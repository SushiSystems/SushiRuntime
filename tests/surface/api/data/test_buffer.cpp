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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
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
#include <memory>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/allocation.hpp>

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
        bool shared_allocation_honours(Runtime& runtime, std::size_t alignment)
        {
            sycl::queue& queue =
                SushiRuntime::Internal::ContextAccess::context(runtime).devices().queue(0);
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

        /** @brief Expects a handle to accept a zero-length window at its end. */
        template <class Handle>
        void expect_empty_window_at_the_end_accepted(const Handle& handle)
        {
            const ElementRange at_end{handle.size(), 0};
            EXPECT_TRUE(handle.read_range(at_end).value().empty());
            EXPECT_NO_THROW(handle.write_range(at_end, span<const int>()).value());
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
    } // namespace

    /**
     * @brief Tests that every range call of a Buffer stops on a window one past its end.
     */
    TEST(Medium_DataHandlesDeathTest, BufferStopsOnAWindowPastTheEnd)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE).value();

        expect_every_range_call_stops(buf, "Buffer", {K_SIZE, 1});
        expect_every_range_call_stops(buf, "Buffer", {K_SIZE - 2, 5});
    }

    /**
     * @brief Tests that every range call of a State stops on a window one past its end.
     */
    TEST(Medium_DataHandlesDeathTest, StateStopsOnAWindowPastTheEnd)
    {
        auto rt = Runtime::create().value();
        const auto field = rt.state<int>(K_SIZE).value();

        expect_every_range_call_stops(field, "State", {K_SIZE, 1});
        expect_every_range_call_stops(field, "State", {K_SIZE - 2, 5});
    }

    /**
     * @brief Tests that a window whose offset plus count wraps around stops the call, not wraps.
     */
    TEST(Medium_DataHandlesDeathTest, WindowWhoseEndWrapsStops)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE).value();

        const std::size_t most = std::numeric_limits<std::size_t>::max();
        expect_every_range_call_stops(buf, "Buffer", {most, 2});
        expect_every_range_call_stops(buf, "Buffer", {2, most});
    }

    /**
     * @brief Tests that a zero-length window at the end of a Buffer or a State is accepted.
     */
    TEST(Medium_DataHandles, EmptyWindowAtTheEndIsAccepted)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE).value();
        const auto field = rt.state<int>(K_SIZE).value();

        expect_empty_window_at_the_end_accepted(buf);
        expect_empty_window_at_the_end_accepted(field);
    }

    /**
     * @brief Tests that a window which fits round-trips through write_range and read_range.
     */
    TEST(Medium_DataHandles, FittingWindowRoundTrips)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE).value();
        for (int& element : buf.host())
            element = 0;

        const std::vector<int> source{11, 12, 13, 14, 15};
        buf.write_range({K_SIZE - 5, 5}, source).value();

        EXPECT_EQ(buf.read_range({K_SIZE - 5, 5}).value(), source);
        EXPECT_EQ(buf[K_SIZE - 6], 0);
    }

    /**
     * @brief Tests that write_range stops on a null source and a short source for a non-empty window.
     */
    TEST(Medium_DataHandlesDeathTest, NullOrShortSourceWithACountStops)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE).value();
        const auto field = rt.state<int>(K_SIZE).value();
        const std::vector<int> two(2, 1);

        EXPECT_DEATH(static_cast<void>(buf.write_range({0, 4}, span<const int>())),
                     "Buffer::write_range: the source is null for a window of count 4");
        EXPECT_DEATH(static_cast<void>(field.write_range({0, 4}, span<const int>())),
                     "State::write_range: the source is null");
        EXPECT_DEATH(static_cast<void>(buf.write_range({0, 4}, two)),
                     "the source holds 2 elements for a window of count 4");
    }

    /**
     * @brief Tests that an element count whose byte size overflows stops before any allocation.
     */
    TEST(Medium_DataHandlesDeathTest, OverflowingElementCountStopsBeforeAllocating)
    {
        auto rt = Runtime::create().value();
        // The byte size of this count wraps to 8, which an unchecked allocation would grant.
        const std::size_t count = std::numeric_limits<std::size_t>::max() / sizeof(double) + 2;
        const std::string pattern = "Buffer allocation: " + std::to_string(count) + " elements";

        EXPECT_DEATH(static_cast<void>(rt.buffer<double>(count)), pattern);
        EXPECT_DEATH(static_cast<void>(rt.state<double>(count)), pattern);
    }

    /**
     * @brief Tests that a State and a Buffer answer the queries they share alike.
     */
    TEST(Medium_DataHandles, StateAndBufferAnswerTheSharedQueriesAlike)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}).value();
        const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}).value();

        EXPECT_EQ(field.device_index(), DeviceIndex{0});
        EXPECT_EQ(field.device_index(), buf.device_index());
        EXPECT_EQ(field.size(), buf.size());
        EXPECT_EQ(field.empty(), buf.empty());
        EXPECT_EQ(field.residency(), buf.residency());
        EXPECT_EQ(field.is_host_accessible(), buf.is_host_accessible());

        const BufferRegion window = field.region({4, 8});
        EXPECT_EQ(window.base, static_cast<void*>(field.current()));
        EXPECT_EQ(window.offset, 4 * sizeof(int));
        EXPECT_EQ(window.length, 8 * sizeof(int));
    }

    /**
     * @brief Tests that a trivially copyable struct instantiates Buffer and State and round-trips.
     */
    TEST(Medium_DataHandles, TriviallyCopyableStructRoundTrips)
    {
        EXPECT_TRUE(std::is_trivially_copyable<Pair>::value);

        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<Pair>(2).value();
        const auto field = rt.state<Pair>(2).value();
        const std::vector<Pair> source{{1.5f, -2.0f}, {3.25f, 4.0f}};
        buf.write_range({0, 2}, source).value();
        field.write_range({0, 2}, source).value();

        // A round trip copies bits, so the absolute tolerance only bounds the comparison.
        constexpr float K_TOLERANCE = 1e-6f;
        const std::vector<Pair> from_buffer = buf.read_range({0, 2}).value();
        const std::vector<Pair> from_state = field.read_range({0, 2}).value();
        ASSERT_EQ(from_buffer.size(), source.size());
        ASSERT_EQ(from_state.size(), source.size());
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            EXPECT_NEAR(from_buffer[i].x, source[i].x, K_TOLERANCE) << "at i=" << i;
            EXPECT_NEAR(from_buffer[i].y, source[i].y, K_TOLERANCE) << "at i=" << i;
            EXPECT_NEAR(from_state[i].x, source[i].x, K_TOLERANCE) << "at i=" << i;
            EXPECT_NEAR(from_state[i].y, source[i].y, K_TOLERANCE) << "at i=" << i;
        }
    }

    /**
     * @brief Tests that host() on a device-resident handle stops and names the handle.
     */
    TEST(Medium_DataHandlesDeathTest, HostOnADeviceResidentHandleStops)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
        const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();

        EXPECT_DEATH(static_cast<void>(buf.host()), "Buffer::host: a device-resident buffer");
        EXPECT_DEATH(static_cast<void>(field.host()), "State::host: a device-resident buffer");
    }

    #if SR_ENABLE_ASSERTIONS
    /**
     * @brief Tests that operator[] on a device-resident handle stops where SR_ASSERT is checked.
     */
    TEST(Medium_DataHandlesDeathTest, IndexingADeviceResidentHandleStops)
    {
        auto rt = Runtime::create().value();
        const auto buf = rt.buffer<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();
        const auto field = rt.state<int>(K_SIZE, DeviceIndex{0}, Residency::Device).value();

        EXPECT_DEATH(static_cast<void>(buf[0]), "Buffer::operator.*a device-resident buffer");
        EXPECT_DEATH(static_cast<void>(field[0]), "State::operator.*a device-resident state");
    }
    #endif

    /**
     * @brief Tests that buffer() and state() stop on a device index that names no device.
     */
    TEST(Medium_DataHandlesDeathTest, DeviceIndexThatNamesNoDeviceStops)
    {
        auto rt = Runtime::create().value();
        const std::size_t count = rt.advanced().device_count();
        const DeviceIndex past_the_end{count};
        const std::string detail = ": device index " + std::to_string(count) +
                                   " names no device; the runtime drives " +
                                   std::to_string(count) + " devices, and advanced";

        EXPECT_DEATH(static_cast<void>(rt.buffer<float>(8, past_the_end)),
                     "Runtime::buffer" + detail);
        EXPECT_DEATH(static_cast<void>(rt.state<float>(8, past_the_end)),
                     "Runtime::state" + detail);
    }

    /**
     * @brief Tests that an aligned buffer and an aligned state start on a multiple of the alignment,
     *        for each residency the device offers.
     */
    TEST(Medium_DataHandles, AlignedAllocationStartsOnAMultipleOfTheAlignment)
    {
        constexpr std::size_t K_ALIGNMENT = 128;
        const auto is_aligned = [](const float* address)
        {
            return reinterpret_cast<std::uintptr_t>(address) % K_ALIGNMENT == 0u;
        };

        auto rt = Runtime::create().value();
        std::size_t residencies_checked = 0;
        for (const Residency residency : {Residency::Shared, Residency::Host, Residency::Device})
        {
            SCOPED_TRACE(residency_name(residency));
            if (residency == Residency::Shared && !shared_allocation_honours(rt, K_ALIGNMENT))
            {
                RecordProperty("shared_alignment_not_honoured", std::to_string(K_ALIGNMENT));
                continue;
            }
            const auto buf = rt.buffer<float>(K_SIZE, DeviceIndex{0}, residency, K_ALIGNMENT);
            const auto field = rt.state<float>(K_SIZE, DeviceIndex{0}, residency, K_ALIGNMENT);
            // A residency the device does not offer returns AllocationFailed and is not checked.
            if (!buf || !field)
                continue;

            ++residencies_checked;
            EXPECT_TRUE(is_aligned(buf->data()));
            EXPECT_TRUE(is_aligned(field->current()));
            EXPECT_TRUE(is_aligned(field->next()));
        }
        EXPECT_GT(residencies_checked, 0u) << "the device offered no residency to check";
    }

    /** @brief Tests that buffer() and state() stop on an alignment that is not a power of two. */
    TEST(Medium_DataHandlesDeathTest, AlignmentThatIsNotAPowerOfTwoStops)
    {
        auto rt = Runtime::create().value();

        EXPECT_DEATH(static_cast<void>(rt.buffer<float>(K_SIZE, DeviceIndex{0}, Residency::Shared, 48)),
                     "Runtime::buffer: alignment 48 is not a power of two");
        EXPECT_DEATH(static_cast<void>(rt.state<float>(K_SIZE, DeviceIndex{0}, Residency::Shared, 48)),
                     "Runtime::state: alignment 48 is not a power of two");
    }
} // namespace DataHandlesTests

/** @brief Tests buffers and States allocated with Residency::Device, Shared and Host. */
namespace DeviceResidentTests
{
    /**
     * @brief Tests that a device-resident buffer round-trips a host seed, a kernel and a host readback.
     */
    TEST(Medium_DeviceResident, BufferRoundTrip)
    {
        constexpr std::size_t N = 128;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();

        EXPECT_EQ(buf.residency(), Residency::Device);
        EXPECT_FALSE(buf.is_host_accessible());

        std::vector<int> seed(N);
        std::iota(seed.begin(), seed.end(), 0);     // 0,1,2,...
        buf.write_range({0, N}, seed).value();

        int* p = buf.data();
        auto g = rt.graph();
        g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] *= 2; });
        g.run().value();

        const std::vector<int> out = buf.read_range({0, N}).value();
        ASSERT_EQ(out.size(), N);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(out[i], static_cast<int>(i) * 2) << "at i=" << i;
    }

    // read_range can pull a window without copying the whole field.
    TEST(Medium_DeviceResident, SelectiveReadback)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();

        std::vector<int> seed(N);
        std::iota(seed.begin(), seed.end(), 100);   // 100,101,...
        buf.write_range({0, N}, seed).value();

        const std::vector<int> window = buf.read_range({8, 4}).value();
        ASSERT_EQ(window.size(), 4u);
        for (std::size_t k = 0; k < 4; ++k)
            EXPECT_EQ(window[k], static_cast<int>(108 + k));
    }

    /**
     * @brief Tests that a device-resident State steps and is seeded and read back through its range
     *        API.
     */
    TEST(Medium_DeviceResident, StateStepsResident)
    {
        constexpr std::size_t N     = 96;
        constexpr std::size_t STEPS = 5;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N, DeviceIndex{0}, Residency::Device).value();

        EXPECT_EQ(field.residency(), Residency::Device);
        EXPECT_FALSE(field.is_host_accessible());

        std::vector<int> seed(N, 0);
        field.write_range({0, N}, seed).value();

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
        g.run(STEPS).value();

        const std::vector<int> out = field.read_range({0, N}).value();
        ASSERT_EQ(out.size(), N);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(out[i], static_cast<int>(STEPS)) << "at i=" << i;
    }

    /** @brief Tests that Shared residency is the default and stays host-addressable. */
    TEST(Medium_DeviceResident, SharedDefaultUnchanged)
    {
        constexpr std::size_t N = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();

        EXPECT_EQ(buf.residency(), Residency::Shared);
        EXPECT_TRUE(buf.is_host_accessible());

        for (std::size_t i = 0; i < N; ++i)
            buf[i] = static_cast<int>(i);           // direct host indexing still valid

        const std::vector<int> out = buf.read_range({0, N}).value();
        for (std::size_t i = 0; i < N; ++i)
            EXPECT_EQ(out[i], static_cast<int>(i));
    }

    /** @brief Tests that host() returns a span that indexes the whole shared buffer. */
    TEST(Medium_DeviceResident, HostSpanIndexesSharedBuffer)
    {
        constexpr std::size_t N = 16;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();

        auto view = buf.host();
        ASSERT_EQ(view.size(), N);
        for (std::size_t i = 0; i < N; ++i)
            view[i] = static_cast<int>(i * 2);

        const std::vector<int> out = buf.read_range({0, N}).value();
        for (std::size_t i = 0; i < N; ++i)
            EXPECT_EQ(out[i], static_cast<int>(i * 2));
    }

    /** @brief Tests that host() on a device-resident buffer stops the process. */
    TEST(Medium_DeviceResidentDeathTest, HostOnADeviceBufferStops)
    {
        constexpr std::size_t N = 16;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Device).value();
        ASSERT_FALSE(buf.is_host_accessible());

        EXPECT_DEATH(static_cast<void>(buf.host()), "Buffer::host: a device-resident buffer");
    }

    /**
     * @brief Tests that a kernel writes a host-resident buffer and the host reads it by direct
     *        indexing.
     */
    TEST(Medium_DeviceResident, HostBufferIsWrittenByAKernelAndReadOnTheHost)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N, DeviceIndex{0}, Residency::Host).value();

        EXPECT_EQ(buf.residency(), Residency::Host);
        EXPECT_TRUE(buf.is_host_accessible());

        for (std::size_t i = 0; i < N; ++i)
            buf[i] = static_cast<int>(i);

        int* p = buf.data();
        auto g = rt.graph();
        g.add(N, Reads(buf), Writes(buf), [p](std::size_t i) { p[i] += 1000; });
        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i) + 1000) << "at i=" << i;
    }
} // namespace DeviceResidentTests

/** @brief Tests that a Buffer adopting an allocation releases its address exactly once. */
namespace AllocationTests
{
    using SushiRuntime::Buffer;
    using SushiRuntime::DeviceIndex;
    using SushiRuntime::Residency;
    using SushiRuntime::Detail::HandleAccess;

    namespace
    {
        /** @brief Returns a distinct non-null address that is never dereferenced. */
        void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 4096u); }

        /** @brief Builds a block whose owner counts its one release, for Buffer tests. */
        SushiRuntime::Detail::MemoryBlock counted_block(std::uintptr_t address, std::size_t bytes,
                                                        int& releases, void*& last)
        {
            SushiRuntime::Detail::MemoryBlock block;
            block.address = fake(address);
            block.bytes = bytes;
            block.owner = std::shared_ptr<void>(fake(address), [&releases, &last](void* released)
            {
                ++releases;
                last = released;
            });
            return block;
        }
    }

    TEST(Medium_Buffer, AdoptingConstructorExposesTheAdoptedAddress)
    {
        int releases = 0;
        void* last = nullptr;
        auto buffer = HandleAccess::construct<Buffer<std::uint8_t>>(
            SushiRuntime::ContextHandle(), counted_block(5, 16, releases, last), 16,
            DeviceIndex{0}, Residency::Shared);
        EXPECT_EQ(buffer.data(), fake(5));
        EXPECT_EQ(releases, 0);
    }

    TEST(Medium_Buffer, DestructionReleasesTheAdoptedAllocationOnce)
    {
        int releases = 0;
        void* last = nullptr;
        {
            auto buffer = HandleAccess::construct<Buffer<std::uint8_t>>(
                SushiRuntime::ContextHandle(), counted_block(6, 16, releases, last), 16,
                DeviceIndex{0}, Residency::Shared);
        }
        EXPECT_EQ(releases, 1);
        EXPECT_EQ(last, fake(6));
    }

    TEST(Medium_BufferDeathTest, AdoptingATooSmallAllocationStops)
    {
        int releases = 0;
        void* last = nullptr;
        EXPECT_DEATH(
            {
                auto buffer = HandleAccess::construct<Buffer<std::uint32_t>>(
                    SushiRuntime::ContextHandle(), counted_block(7, 4, releases, last), 4,
                    DeviceIndex{0}, Residency::Shared);
            },
            "Buffer adoption: an allocation of 4 bytes cannot hold 4 elements");
    }

    TEST(Medium_BufferDeathTest, AdoptingACountThatOverflowsItsByteSizeStops)
    {
        int releases = 0;
        void* last = nullptr;
        const std::size_t huge = std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;
        EXPECT_DEATH(
            {
                auto buffer = HandleAccess::construct<Buffer<std::uint32_t>>(
                    SushiRuntime::ContextHandle(), counted_block(8, 4, releases, last), huge,
                    DeviceIndex{0}, Residency::Shared);
            },
            "Buffer adoption: " + std::to_string(huge) + " elements");
    }
} // namespace AllocationTests
