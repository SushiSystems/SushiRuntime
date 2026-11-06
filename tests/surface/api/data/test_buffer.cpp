/**************************************************************************/
/* test_buffer.cpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_buffer.cpp
 * @brief Tests Buffer, an adopting Buffer, and the windows and residency it shares with State.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/data/buffer.hpp>

#include "support/adopted_handles.hpp"
#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests the range, allocation, residency and element contracts of Buffer and State. */
namespace DataHandlesTests
{
    namespace
    {
        /** @brief Element count of every handle the range tests allocate. */
        constexpr std::size_t K_SIZE = 37;

        /**
         * @brief Returns whether the toolchain's aligned shared allocation on device 0 honours
         *        @p alignment.
         *
         * Several allocations are live at once, so a heap that happens to hand out one aligned
         * address does not pass the probe.
         */
        bool shared_allocation_honours(const Runtime& runtime, std::size_t alignment)
        {
            sycl::queue& queue = runtime.advanced().queue(DeviceIndex{0});
            constexpr std::size_t K_PROBES = 8;
            std::array<void*, K_PROBES> probes{};
            bool honoured = true;
            for (void*& probe : probes)
            {
                probe = sycl::aligned_alloc_shared(alignment, 1, queue);
                honoured = honoured && probe != nullptr
                           && reinterpret_cast<std::uintptr_t>(probe) % alignment == 0u;
            }
            for (void* const probe : probes)
                sycl::free(probe, queue);
            return honoured;
        }

        /** @brief Returns the pattern of the line a call writes when it stops on a window. */
        std::string window_pattern(const std::string& call, ElementRange range, std::size_t size)
        {
            return call + ".*offset " + std::to_string(range.offset) + ", count " +
                   std::to_string(range.count) + ".*size " + std::to_string(size);
        }

        /**
         * @brief Expects read_range, write_range and region of a handle to stop on one window.
         * @param name "Buffer" or "State", as the messages spell the handle.
         */
        template <class Handle>
        void expect_every_range_call_stops(const Handle& handle, const std::string& name,
                                           ElementRange range)
        {
            const std::vector<int> source(K_SIZE, 1);
            EXPECT_DEATH(static_cast<void>(handle.read_range(range)),
                         window_pattern(name + "::read_range", range, handle.size()));
            EXPECT_DEATH(static_cast<void>(handle.write_range(range, source)),
                         window_pattern(name + "::write_range", range, handle.size()));
            EXPECT_DEATH(static_cast<void>(handle.region(range)),
                         window_pattern(name + "::region", range, handle.size()));
        }

        /** @brief Expects every range call of a Buffer allocated here to stop on @p range. */
        void expect_buffer_range_calls_stop(const Runtime& runtime, ElementRange range)
        {
            const auto buffer = runtime.buffer<int>(K_SIZE).value();
            expect_every_range_call_stops(buffer, "Buffer", range);
        }

        /** @brief Expects every range call of a State allocated here to stop on @p range. */
        void expect_state_range_calls_stop(const Runtime& runtime, ElementRange range)
        {
            const auto field = runtime.state<int>(K_SIZE).value();
            expect_every_range_call_stops(field, "State", range);
        }

        /** @brief Expects a handle to accept a zero-length window at its end. */
        template <class Handle>
        void expect_empty_window_at_the_end_accepted(const Handle& handle)
        {
            const ElementRange at_end{handle.size(), 0};
            EXPECT_TRUE(handle.read_range(at_end).value().empty());
            EXPECT_TRUE(handle.write_range(at_end, span<const int>()).has_value());
            const BufferRegion region = handle.region(at_end);
            EXPECT_EQ(region.offset, handle.size() * sizeof(int));
            EXPECT_EQ(region.length, 0u);
        }

        /** @brief A trivially copyable element type of two floats. */
        struct Pair
        {
            float x;
            float y;
        };

        /** @brief Compares two pairs by value; the test values are dyadic, so equality is exact. */
        bool operator==(const Pair& left, const Pair& right)
        {
            return left.x == right.x && left.y == right.y;
        }

        /** @brief Names a window row: the handle it is read through and the window it names. */
        struct WindowRow
        {
            const char* name;                                    /**< The row's name in a report. */
            void (*expect_stops)(const Runtime&, ElementRange);  /**< Reads the window through a handle. */
            ElementRange range;                                  /**< The window that does not fit. */
        };

        /** @brief Returns the name a window row carries in a report. */
        std::string window_row_name(const ::testing::TestParamInfo<WindowRow>& info)
        {
            return info.param.name;
        }

        /** @brief Names a handle row: the handle kind and the check made through it. */
        struct HandleRow
        {
            const char* name;                      /**< The row's name in a report. */
            void (*expect_stops)(const Runtime&);  /**< Calls host() on a device-resident handle of the kind. */
        };

        /** @brief Returns the name a handle row carries in a report. */
        std::string handle_row_name(const ::testing::TestParamInfo<HandleRow>& info)
        {
            return info.param.name;
        }

        /** @brief Expects host() on a device-resident Buffer to stop and to name the Buffer. */
        void expect_buffer_host_stops(const Runtime& runtime)
        {
            const auto buffer = runtime.buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
            EXPECT_DEATH(static_cast<void>(buffer.host()), "Buffer::host: a device-resident buffer");
        }

        /** @brief Expects host() on a device-resident State to stop and to name the State. */
        void expect_state_host_stops(const Runtime& runtime)
        {
            const auto field = runtime.state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
            EXPECT_DEATH(static_cast<void>(field.host()), "State::host: a device-resident buffer");
        }

        /** @brief Names an allocation row: the residency an aligned allocation is asked for. */
        struct ResidencyRow
        {
            const char* name;     /**< The row's name in a report. */
            Residency residency;  /**< The residency of the allocation. */
        };

        /** @brief Returns the name a residency row carries in a report. */
        std::string residency_row_name(const ::testing::TestParamInfo<ResidencyRow>& info)
        {
            return info.param.name;
        }
    } // namespace

    /** @brief Fixture of the range tests that allocate handles on the program's runtime. */
    class Medium_DataHandles : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the death tests that allocate handles on the program's runtime. */
    class Medium_DataHandlesDeathTest : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the rows that read a window no handle holds. */
    class Medium_DataHandleWindowDeathTest : public TestSupport::RuntimeTest,
                                             public ::testing::WithParamInterface<WindowRow>
    {
    };

    /** @brief Fixture of the rows that call host() on a device-resident handle. */
    class Medium_DataHandleResidencyDeathTest : public TestSupport::RuntimeTest,
                                                public ::testing::WithParamInterface<HandleRow>
    {
    };

    /**
     * @brief Fixture of the rows that allocate aligned handles; a residency the device does not
     *        offer, or a shared heap that ignores the alignment, skips the row.
     */
    class Medium_DataHandlesAlignment : public TestSupport::RuntimeTest,
                                        public ::testing::WithParamInterface<ResidencyRow>
    {
        protected:
            /** @brief Alignment, in bytes, every row asks for. */
            static constexpr std::size_t K_ALIGNMENT = 128;

            /** @brief Skips the row when the device cannot honour it. */
            void SetUp() override
            {
                if (GetParam().residency == Residency::Shared &&
                    !shared_allocation_honours(runtime(), K_ALIGNMENT))
                    GTEST_SKIP() << "the toolchain's shared allocation ignores an alignment of "
                                 << K_ALIGNMENT;
                if (!runtime().buffer<float>(K_SIZE, DeviceIndex{0}, GetParam().residency, K_ALIGNMENT))
                    GTEST_SKIP() << "the device does not offer "
                                 << residency_name(GetParam().residency);
            }
    };

    /** @brief Tests that every range call stops on a window that does not fit the handle. */
    TEST_P(Medium_DataHandleWindowDeathTest, StopsOnAWindowThatDoesNotFit)
    {
        GetParam().expect_stops(runtime(), GetParam().range);
    }

    INSTANTIATE_TEST_SUITE_P(
        Rows, Medium_DataHandleWindowDeathTest,
        ::testing::Values(
            WindowRow{"BufferOnePastTheEnd", expect_buffer_range_calls_stop, {K_SIZE, 1}},
            WindowRow{"BufferStartsInsideAndRunsOver", expect_buffer_range_calls_stop,
                      {K_SIZE - 2, 5}},
            WindowRow{"StateOnePastTheEnd", expect_state_range_calls_stop, {K_SIZE, 1}},
            WindowRow{"StateStartsInsideAndRunsOver", expect_state_range_calls_stop,
                      {K_SIZE - 2, 5}},
            WindowRow{"BufferOffsetNearTheLargestSize", expect_buffer_range_calls_stop,
                      {std::numeric_limits<std::size_t>::max(), 2}},
            WindowRow{"BufferCountNearTheLargestSize", expect_buffer_range_calls_stop,
                      {2, std::numeric_limits<std::size_t>::max()}}),
        window_row_name);

    /** @brief Tests that a zero-length window at the end of a Buffer is accepted. */
    TEST_F(Medium_DataHandles, EmptyWindowAtTheEndOfABufferIsAccepted)
    {
        const auto buffer = runtime().buffer<int>(K_SIZE).value();

        expect_empty_window_at_the_end_accepted(buffer);
    }

    /** @brief Tests that a zero-length window at the end of a State is accepted. */
    TEST_F(Medium_DataHandles, EmptyWindowAtTheEndOfAStateIsAccepted)
    {
        const auto field = runtime().state<int>(K_SIZE).value();

        expect_empty_window_at_the_end_accepted(field);
    }

    /** @brief Tests that a window which fits round-trips through write_range and read_range. */
    TEST_F(Medium_DataHandles, FittingWindowRoundTrips)
    {
        const auto buffer = runtime().buffer<int>(K_SIZE).value();
        TestSupport::fill_with(buffer, 0);

        const std::vector<int> source{11, 12, 13, 14, 15};
        buffer.write_range({K_SIZE - 5, 5}, source).value();

        EXPECT_EQ(buffer.read_range({K_SIZE - 5, 5}).value(), source);
        EXPECT_EQ(buffer[K_SIZE - 6], 0);
    }

    /** @brief Tests that write_range stops on a null source for a non-empty window of a Buffer. */
    TEST_F(Medium_DataHandlesDeathTest, ANullSourceWithACountStopsABuffer)
    {
        const auto buffer = runtime().buffer<int>(K_SIZE).value();

        EXPECT_DEATH(static_cast<void>(buffer.write_range({0, 4}, span<const int>())),
                     "Buffer::write_range: the source is null for a window of count 4");
    }

    /** @brief Tests that write_range stops on a null source for a non-empty window of a State. */
    TEST_F(Medium_DataHandlesDeathTest, ANullSourceWithACountStopsAState)
    {
        const auto field = runtime().state<int>(K_SIZE).value();

        EXPECT_DEATH(static_cast<void>(field.write_range({0, 4}, span<const int>())),
                     "State::write_range: the source is null");
    }

    /** @brief Tests that write_range stops on a source shorter than the window. */
    TEST_F(Medium_DataHandlesDeathTest, AShortSourceStops)
    {
        const auto buffer = runtime().buffer<int>(K_SIZE).value();
        const std::vector<int> two(2, 1);

        EXPECT_DEATH(static_cast<void>(buffer.write_range({0, 4}, two)),
                     "the source holds 2 elements for a window of count 4");
    }

    /** @brief Tests that an element count whose byte size overflows stops before any allocation. */
    TEST_F(Medium_DataHandlesDeathTest, OverflowingElementCountStopsBeforeAllocating)
    {
        // The byte size of this count wraps to 8, which an unchecked allocation would grant.
        const std::size_t count = std::numeric_limits<std::size_t>::max() / sizeof(double) + 2;
        const std::string pattern = "Buffer allocation: " + std::to_string(count) + " elements";

        EXPECT_DEATH(static_cast<void>(runtime().buffer<double>(count)), pattern);
        EXPECT_DEATH(static_cast<void>(runtime().state<double>(count)), pattern);
    }

    /** @brief Tests that a State and a Buffer allocated alike report the same metadata. */
    TEST_F(Medium_DataHandles, StateAndBufferReportTheSameMetadata)
    {
        const auto buffer = runtime().buffer<int>(K_SIZE, DeviceIndex{0}).value();
        const auto field = runtime().state<int>(K_SIZE, DeviceIndex{0}).value();

        EXPECT_EQ(field.device_index(), buffer.device_index());
        EXPECT_EQ(field.size(), buffer.size());
        EXPECT_EQ(field.empty(), buffer.empty());
        EXPECT_EQ(field.residency(), buffer.residency());
        EXPECT_EQ(field.is_host_accessible(), buffer.is_host_accessible());
    }

    /** @brief Tests that State::region names a byte window of the current field. */
    TEST_F(Medium_DataHandles, StateRegionNamesAWindowOfTheCurrentField)
    {
        const auto field = runtime().state<int>(K_SIZE, DeviceIndex{0}).value();

        const BufferRegion window = field.region({4, 8});

        EXPECT_EQ(window.base, static_cast<void*>(field.current()));
        EXPECT_EQ(window.offset, 4 * sizeof(int));
        EXPECT_EQ(window.length, 8 * sizeof(int));
    }

    /** @brief Tests that a trivially copyable struct instantiates Buffer and round-trips. */
    TEST_F(Medium_DataHandles, TriviallyCopyableStructRoundTripsThroughABuffer)
    {
        const auto buffer = runtime().buffer<Pair>(2).value();
        const std::vector<Pair> source{{1.5f, -2.0f}, {3.25f, 4.0f}};

        buffer.write_range({0, 2}, source).value();

        // The copy moves bits and the values are dyadic, so equality is the oracle.
        EXPECT_EQ(buffer.read_range({0, 2}).value(), source);
    }

    /** @brief Tests that a trivially copyable struct instantiates State and round-trips. */
    TEST_F(Medium_DataHandles, TriviallyCopyableStructRoundTripsThroughAState)
    {
        const auto field = runtime().state<Pair>(2).value();
        const std::vector<Pair> source{{1.5f, -2.0f}, {3.25f, 4.0f}};

        field.write_range({0, 2}, source).value();

        EXPECT_EQ(field.read_range({0, 2}).value(), source);
    }

    /** @brief Tests that host() on a device-resident Buffer or State stops and names the handle. */
    TEST_P(Medium_DataHandleResidencyDeathTest, HostOnADeviceResidentHandleStops)
    {
        GetParam().expect_stops(runtime());
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_DataHandleResidencyDeathTest,
                             ::testing::Values(HandleRow{"Buffer", expect_buffer_host_stops},
                                               HandleRow{"State", expect_state_host_stops}),
                             handle_row_name);

    #if SR_ENABLE_ASSERTIONS
    /** @brief Tests that operator[] on a device-resident handle stops where SR_ASSERT is checked. */
    TEST_F(Medium_DataHandlesDeathTest, IndexingADeviceResidentHandleStops)
    {
        const auto buffer =
            runtime().buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
        const auto field = runtime().state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();

        EXPECT_DEATH(static_cast<void>(buffer[0]), "Buffer::operator.*a device-resident buffer");
        EXPECT_DEATH(static_cast<void>(field[0]), "State::operator.*a device-resident state");
    }
    #endif

    /** @brief Tests that buffer() and state() stop on a device index that names no device. */
    TEST_F(Medium_DataHandlesDeathTest, DeviceIndexThatNamesNoDeviceStops)
    {
        const std::size_t count = runtime().advanced().device_count();
        const DeviceIndex past_the_end{count};
        const std::string detail = ": device index " + std::to_string(count) +
                                   " names no device; the runtime drives " +
                                   std::to_string(count) + " devices, and advanced";

        EXPECT_DEATH(static_cast<void>(runtime().buffer<float>(8, past_the_end)),
                     "Runtime::buffer" + detail);
        EXPECT_DEATH(static_cast<void>(runtime().state<float>(8, past_the_end)),
                     "Runtime::state" + detail);
    }

    /** @brief Tests that an aligned buffer and an aligned state start on a multiple of the alignment. */
    TEST_P(Medium_DataHandlesAlignment, AlignedAllocationStartsOnAMultipleOfTheAlignment)
    {
        const auto is_aligned = [](const float* address)
        {
            return reinterpret_cast<std::uintptr_t>(address) % K_ALIGNMENT == 0u;
        };

        const auto buffer =
            runtime().buffer<float>(K_SIZE, DeviceIndex{0}, GetParam().residency, K_ALIGNMENT).value();
        const auto field =
            runtime().state<float>(K_SIZE, DeviceIndex{0}, GetParam().residency, K_ALIGNMENT).value();

        EXPECT_TRUE(is_aligned(buffer.data()));
        EXPECT_TRUE(is_aligned(field.current()));
        EXPECT_TRUE(is_aligned(field.next()));
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_DataHandlesAlignment,
                             ::testing::Values(ResidencyRow{"Shared", Residency::Shared},
                                               ResidencyRow{"Host", Residency::Host},
                                               ResidencyRow{"Device", Residency::Device}),
                             residency_row_name);

    /** @brief Tests that buffer() and state() stop on an alignment that is not a power of two. */
    TEST_F(Medium_DataHandlesDeathTest, AlignmentThatIsNotAPowerOfTwoStops)
    {
        EXPECT_DEATH(static_cast<void>(
                         runtime().buffer<float>(K_SIZE, DeviceIndex{0}, Residency::Shared, 48)),
                     "Runtime::buffer: alignment 48 is not a power of two");
        EXPECT_DEATH(static_cast<void>(
                         runtime().state<float>(K_SIZE, DeviceIndex{0}, Residency::Shared, 48)),
                     "Runtime::state: alignment 48 is not a power of two");
    }
} // namespace DataHandlesTests

/** @brief Tests buffers and States allocated with Residency::Device, Shared and Host. */
namespace DeviceResidentTests
{
    namespace
    {
        /** @brief Returns @p values, each multiplied by @p factor. */
        std::vector<int> scaled(std::vector<int> values, int factor)
        {
            for (int& value : values)
                value *= factor;
            return values;
        }
    } // namespace

    /** @brief Fixture of the tests that allocate with a residency on the program's runtime. */
    class Medium_DeviceResident : public TestSupport::RuntimeTest
    {
    };

    /** @brief Tests that a device-resident buffer round-trips a host seed, a kernel and a host readback. */
    TEST_F(Medium_DeviceResident, BufferRoundTrip)
    {
        constexpr std::size_t N = 128;

        const Runtime& rt = runtime();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();
        const std::vector<int> seed = TestSupport::ramp<int>(N);
        buf.write_range({0, N}, seed).value();

        int* p = buf.data();
        auto g = rt.graph();
        g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] *= 2; });
        g.run().value();

        EXPECT_EQ(buf.read_range({0, N}).value(), scaled(seed, 2));
    }

    /** @brief Tests that a buffer allocated on the device reports Device residency and no host access. */
    TEST_F(Medium_DeviceResident, ABufferAllocatedOnTheDeviceIsNotHostAccessible)
    {
        const auto buf = runtime().buffer<int>(16, DeviceIndex{0}, Residency::Device).value();

        EXPECT_EQ(buf.residency(), Residency::Device);
        EXPECT_FALSE(buf.is_host_accessible());
    }

    /** @brief Tests that read_range returns only the window asked for of a device-resident buffer. */
    TEST_F(Medium_DeviceResident, ReadRangeOfADeviceBufferReturnsOnlyTheWindow)
    {
        constexpr std::size_t N = 64;

        auto buf = runtime().buffer<int>(N, DeviceIndex{0}, Residency::Device).value();
        buf.write_range({0, N}, TestSupport::ramp<int>(N, 100)).value();

        EXPECT_EQ(buf.read_range({8, 4}).value(), TestSupport::ramp<int>(4, 108));
    }

    /** @brief Tests that a device-resident State steps and reads back through its range API. */
    TEST_F(Medium_DeviceResident, StateStepsResident)
    {
        constexpr std::size_t N     = 96;
        constexpr std::size_t STEPS = 5;

        const Runtime& rt = runtime();
        auto field = rt.state<int>(N, DeviceIndex{0}, Residency::Device).value();
        field.write_range({0, N}, std::vector<int>(N, 0)).value();

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
        g.run(STEPS).value();

        EXPECT_EQ(field.read_range({0, N}).value(), std::vector<int>(N, static_cast<int>(STEPS)));
    }

    /** @brief Tests that the host indexes a shared buffer directly and read_range sees what it wrote. */
    TEST_F(Medium_DeviceResident, DirectHostIndexingOfASharedBufferIsSeenByReadRange)
    {
        constexpr std::size_t N = 32;

        auto buf = runtime().buffer<int>(N).value();

        buf[0] = 7;
        buf[N - 1] = 9;

        const std::vector<int> out = buf.read_range({0, N}).value();
        EXPECT_EQ(out.front(), 7);
        EXPECT_EQ(out.back(), 9);
    }

    /** @brief Tests that host() returns a span that indexes the whole shared buffer. */
    TEST_F(Medium_DeviceResident, HostSpanIndexesSharedBuffer)
    {
        constexpr std::size_t N = 16;

        auto buf = runtime().buffer<int>(N).value();

        auto view = buf.host();
        ASSERT_EQ(view.size(), N);
        view[0] = 11;
        view[N - 1] = 22;

        const std::vector<int> out = buf.read_range({0, N}).value();
        EXPECT_EQ(out.front(), 11);
        EXPECT_EQ(out.back(), 22);
    }

    /** @brief Tests that a kernel writes a host-resident buffer and the host reads it by direct indexing. */
    TEST_F(Medium_DeviceResident, HostBufferIsWrittenByAKernelAndReadOnTheHost)
    {
        constexpr std::size_t N = 64;

        const Runtime& rt = runtime();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Host).value();
        TestSupport::assign_from(buf, TestSupport::ramp<int>(N));

        int* p = buf.data();
        auto g = rt.graph();
        g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] += 1000; });
        g.run().value();

        EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(N, 1000));
    }
} // namespace DeviceResidentTests

/** @brief Tests that a Buffer adopting an allocation releases its address exactly once. */
namespace AllocationTests
{
    using TestSupport::adopted_buffer;
    using TestSupport::made_up_address;

    /** @brief Tests that an adopting Buffer exposes the address of the block it took. */
    TEST(Small_Buffer, AdoptingConstructorExposesTheAdoptedAddress)
    {
        std::vector<void*> releases;
        auto buffer = adopted_buffer<std::uint8_t>(5, 16, releases);

        EXPECT_EQ(buffer.data(), made_up_address(5));
        EXPECT_TRUE(releases.empty());
    }

    /** @brief Tests that destroying an adopting Buffer releases its address once. */
    TEST(Small_Buffer, DestructionReleasesTheAdoptedAllocationOnce)
    {
        std::vector<void*> releases;
        {
            auto buffer = adopted_buffer<std::uint8_t>(6, 16, releases);
        }

        EXPECT_EQ(releases, std::vector<void*>{made_up_address(6)});
    }

    /** @brief Tests that a moved adopting Buffer releases the address once and hands it to the target. */
    TEST(Small_Buffer, MovingAnAdoptedBufferReleasesTheAllocationOnce)
    {
        std::vector<void*> releases;
        {
            auto source = adopted_buffer<std::uint8_t>(9, 16, releases);
            Buffer<std::uint8_t> target(std::move(source));
            EXPECT_TRUE(source.empty());
            EXPECT_EQ(target.data(), made_up_address(9));
            EXPECT_TRUE(releases.empty());
        }

        EXPECT_EQ(releases, std::vector<void*>{made_up_address(9)});
    }

    /** @brief Tests that adopting a block too small for the element count stops the process. */
    TEST(Medium_BufferDeathTest, AdoptingATooSmallAllocationStops)
    {
        std::vector<void*> releases;

        EXPECT_DEATH(static_cast<void>(adopted_buffer<std::uint32_t>(7, 4, 4, releases)),
                     "Buffer adoption: an allocation of 4 bytes cannot hold 4 elements");
    }

    /** @brief Tests that adopting with a count whose byte size overflows stops the process. */
    TEST(Medium_BufferDeathTest, AdoptingACountThatOverflowsItsByteSizeStops)
    {
        std::vector<void*> releases;
        const std::size_t huge =
            std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;

        EXPECT_DEATH(static_cast<void>(adopted_buffer<std::uint32_t>(8, huge, 4, releases)),
                     "Buffer adoption: " + std::to_string(huge) + " elements");
    }
} // namespace AllocationTests
