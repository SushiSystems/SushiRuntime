/**************************************************************************/
/* test_late_count.cpp                                                    */
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
 * @file test_late_count.cpp
 * @brief Tests LateCount and its host and device factories.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"
#include "support/slot_buffers.hpp"

using namespace SushiRuntime;

namespace
{
    constexpr std::uint32_t K_CANARY = 0xDEADBEEFu;

    /** @brief Detects a data member named capacity that a caller can assign. */
    template <typename T, typename = void>
    struct HasAssignableCapacity : std::false_type
    {
    };

    template <typename T>
    struct HasAssignableCapacity<
        T, std::void_t<decltype(std::declval<T&>().capacity = std::size_t{})>> : std::true_type
    {
    };

    /** @brief Detects a public member named when_pred. */
    template <typename T, typename = void>
    struct HasPublicWhenPredicate : std::false_type
    {
    };

    template <typename T>
    struct HasPublicWhenPredicate<T, std::void_t<decltype(std::declval<T&>().when_pred)>>
        : std::true_type
    {
    };

    /** @brief Detects a size setter that takes a bare pointer and an index. */
    template <typename T, typename = void>
    struct HasRawPointerSizeSetter : std::false_type
    {
    };

    template <typename T>
    struct HasRawPointerSizeSetter<
        T, std::void_t<decltype(std::declval<T&>().and_sized_from_device(
               std::declval<const std::uint32_t*>(), std::size_t{}))>> : std::true_type
    {
    };

    /** @brief Detects a size setter that takes a bare pointer under the new name. */
    template <typename T, typename = void>
    struct HasRawPointerSizedFromDevice : std::false_type
    {
    };

    template <typename T>
    struct HasRawPointerSizedFromDevice<
        T, std::void_t<decltype(std::declval<T&>().sized_from_device(
               std::declval<const std::uint32_t*>(), std::size_t{}))>> : std::true_type
    {
    };

    static_assert(!std::is_default_constructible_v<LateCount>,
                  "a LateCount is built by fixed(), from_host() or from_device() only");
    static_assert(std::is_copy_constructible_v<LateCount>,
                  "Recorder::resolve() takes its counts in a std::array, by copy");
    static_assert(!HasAssignableCapacity<LateCount>::value,
                  "the capacity of a LateCount is set when it is built");
    static_assert(std::is_same_v<LateCount::Provider, Dynamic::CountProvider>,
                  "LateCount and Dynamic::sized() take one provider type");
    static_assert(!HasPublicWhenPredicate<Dynamic>::value, "the controls of a Dynamic are private");
    static_assert(!HasRawPointerSizeSetter<Dynamic>::value,
                  "and_sized_from_device(pointer, index) is removed");
    static_assert(!HasRawPointerSizedFromDevice<Dynamic>::value,
                  "sized_from_device() takes the Buffer, never a bare pointer");
    static_assert(std::is_nothrow_move_constructible_v<Dynamic> &&
                      !std::is_copy_constructible_v<Dynamic>,
                  "a Dynamic moves and does not copy");

    /**
     * @brief Records the resolver of one late count; its fill copies the live value to out[0].
     * @param out Receives the live count; must outlive the graph.
     */
    void record_live_count_copy(Recorder& recorder, const LateCount& count,
                                const Buffer<std::uint32_t>& out)
    {
        std::uint32_t* target = out.data();
        recorder.resolve(std::array<LateCount, 1>{count}, out.device_index(),
                         Access(Reads(), Writes(out)),
                         [target](const std::array<std::size_t, 1>& live)
                         {
                             target[0] = static_cast<std::uint32_t>(live[0]);
                         });
    }

    /** @brief Returns a LateCount of the given capacity that reads its live value from slot[0]. */
    LateCount device_count(const Buffer<std::uint32_t>& slot, std::size_t capacity)
    {
        return LateCount::from_device(capacity, slot);
    }

    /**
     * @brief Adds a task of the given capacity that sets marks[i] to one, and runs the graph once.
     * @param marks Holds at least capacity elements, all zero before the call.
     * @return How many elements the task set.
     */
    std::size_t count_elements_run(Runtime& rt, Dynamic window, Buffer<int>& marks, std::size_t capacity)
    {
        int* m = marks.data();
        auto g = rt.graph();
        g.add_dynamic(std::move(window), capacity, Reads{}, Writes(marks),
                      [m](std::size_t i) { m[i] = 1; });
        g.run().value();

        std::size_t ran = 0;
        for (std::size_t i = 0; i < capacity; ++i)
            ran += static_cast<std::size_t>(marks[i]);
        return ran;
    }

    /** @brief Runs @p graph once for each value of @p lives, setting @p live to it first. */
    void run_with_each_live_count(Graph& graph, std::size_t& live, const std::vector<std::size_t>& lives)
    {
        for (const std::size_t next : lives)
        {
            live = next;
            graph.run().value();
        }
    }
} // namespace

/** @brief Fixture of the tests of where Recorder::resolve() takes a LateCount's live value from, on the program's runtime. */
class Medium_LateCount : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Allocates a one-word output buffer that holds the canary. */
        Buffer<std::uint32_t> canary_output()
        {
            auto out = runtime().buffer<std::uint32_t>(1).value();
            out[0] = K_CANARY;
            return out;
        }
};

/** @brief Tests that a host count that changes between runs keeps one compile. */
TEST_F(Medium_LateCount, ChangingHostCountKeepsOneCompile)
{
    auto out = canary_output();
    std::size_t live = 1000;
    const LateCount count = LateCount::from_host(700000, [&live]() { return live; });
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), count, out);

    run_with_each_live_count(g, live, {1000, 700000, 0});

    EXPECT_EQ(g.compile_count(), 1u) << "a live count change between runs must not trigger a recompile";
}

/** @brief Tests that a host count above its capacity fails the run with InvalidGraph naming both numbers. */
TEST_F(Medium_LateCount, HostCountAboveCapacityFailsTheRunWithInvalidGraph)
{
    auto out = canary_output();
    std::size_t live = 101;
    const LateCount count = LateCount::from_host(100, [&live]() { return live; });
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), count, out);

    const auto failed = g.run();

    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidGraph);
    EXPECT_NE(failed.error().message.find("the live count (101) exceeds the capacity"), std::string::npos)
        << failed.error().message;
}

/** @brief Tests that the same graph runs once the provider returns a count that fits. */
TEST_F(Medium_LateCount, ARunGoesOnOnceTheHostCountFitsAgain)
{
    auto out = canary_output();
    std::size_t live = 101;
    const LateCount count = LateCount::from_host(100, [&live]() { return live; });
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), count, out);
    static_cast<void>(g.run());
    live = 100;

    const auto valid = g.run();

    ASSERT_TRUE(valid.has_value()) << valid.error().message;
    EXPECT_EQ(out[0], 100u);
}

/** @brief Tests that a Residency::Host device slot resolves to the value written there. */
TEST_F(Medium_LateCount, DeviceSlotResolvesFromHostResidentBuffer)
{
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out = canary_output();
    counter[0] = 700000;
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), device_count(counter, 1000000), out);

    g.run().value();

    EXPECT_EQ(out[0], 700000u);
}

/** @brief Fixture of the over-capacity tests; the parameter is whether the runtime is forced onto the scheduler. */
class Medium_LateCountLayers : public TestSupport::LayerRuntimeTest
{
    protected:
        /** @brief Chooses the runtime of the row and builds the buffers of the tests. */
        void SetUp() override
        {
            TestSupport::LayerRuntimeTest::SetUp();
            counter_.emplace(runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value());
            out_.emplace(runtime().buffer<std::uint32_t>(1).value());
            (*counter_)[0] = 101;
            (*out_)[0] = K_CANARY;
        }

        /** @brief Drops the buffers. */
        void TearDown() override
        {
            out_.reset();
            counter_.reset();
        }

        /** @brief The slot the graph reads its count from. */
        std::optional<Buffer<std::uint32_t>> counter_;

        /** @brief Receives the live count. */
        std::optional<Buffer<std::uint32_t>> out_;
};

/** @brief Tests that a device count above its capacity fails the run. */
TEST_P(Medium_LateCountLayers, DeviceCountAboveCapacityFailsTheRun)
{
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), device_count(*counter_, 100), *out_);

    EXPECT_FALSE(g.run().has_value());
}

/** @brief Tests that a later graph with a count inside the capacity runs clean after a failed run. */
TEST_P(Medium_LateCountLayers, ALaterGraphRunsCleanAfterAFailedRun)
{
    auto failing = runtime().graph();
    record_live_count_copy(failing.recorder(), device_count(*counter_, 100), *out_);
    static_cast<void>(failing.run());
    (*counter_)[0] = 100;
    auto valid = runtime().graph();
    record_live_count_copy(valid.recorder(), device_count(*counter_, 100), *out_);

    const auto report = valid.run().value();

    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ((*out_)[0], 100u);
}

INSTANTIATE_TEST_SUITE_P(Layers, Medium_LateCountLayers, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info)
                         {
                             return info.param ? "Scheduler" : "Chain";
                         });

/** @brief Holds the first and the next count a predecessor writes. */
struct ProducedCounts
{
    std::uint32_t first;  /**< The count the first run writes. */
    std::uint32_t second; /**< The count the second run writes. */
};

/** @brief Fixture of the predecessor test; the parameter is the two counts. */
class Medium_LateCountProducer : public TestSupport::RuntimeTest,
                                 public ::testing::WithParamInterface<ProducedCounts>
{
};

/** @brief Tests that the resolver sees the count its predecessor wrote in the same run, not one run late. */
TEST_P(Medium_LateCountProducer, ResolverSeesTheCountItsPredecessorJustWrote)
{
    constexpr std::size_t CAPACITY = 1000;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto source  = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = runtime().buffer<std::uint32_t>(1).value();
    counter[0] = 0;
    out[0] = K_CANARY;
    // A device kernel cannot reach this frame, so its next count is in a buffer; see README.md.
    const std::uint32_t* source_ptr = source.data();
    std::uint32_t* counter_ptr = counter.data();
    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads(source), Writes(counter),
          [counter_ptr, source_ptr](std::size_t) { counter_ptr[0] = source_ptr[0]; });
    record_live_count_copy(g.recorder(), device_count(counter, CAPACITY), out);
    source[0] = GetParam().first;
    g.run().value();
    source[0] = GetParam().second;

    g.run().value();

    EXPECT_EQ(out[0], GetParam().second) << "a changed count must take effect the same run, not one run late";
}

INSTANTIATE_TEST_SUITE_P(Counts, Medium_LateCountProducer,
                         ::testing::Values(ProducedCounts{300, 700}, ProducedCounts{700, 0}));

/** @brief Holds one call that must stop the process because of its slot or index, and the text the stop names. */
struct StoppedCountCall
{
    const char* name;                              /**< Names the row. */
    void (*call)(const TestSupport::SlotBuffers&); /**< Makes the call that stops. */
    std::string pattern;                           /**< What the stop message must match. */
};

/** @brief Fixture of the slot death tests; the parameter is the call. */
class Medium_LateCountDeathTest : public TestSupport::RuntimeTest,
                                  public ::testing::WithParamInterface<StoppedCountCall>
{
    protected:
        /** @brief Allocates one buffer of each residency. */
        void SetUp() override { slots_.emplace(TestSupport::make_slot_buffers(runtime())); }

        /** @brief Drops the buffers. */
        void TearDown() override { slots_.reset(); }

        /** @brief Returns the buffers of this test. */
        const TestSupport::SlotBuffers& slots() const { return *slots_; }

    private:
        /** @brief The buffers of this test. */
        std::optional<TestSupport::SlotBuffers> slots_;
};

/** @brief Tests that a LateCount built over a slot the host cannot read, or past it, stops the process. */
TEST_P(Medium_LateCountDeathTest, ASlotThatCannotBeReadStopsTheCallThatNamesIt)
{
    EXPECT_DEATH(GetParam().call(slots()), GetParam().pattern);
}

INSTANTIATE_TEST_SUITE_P(
    Calls, Medium_LateCountDeathTest,
    ::testing::Values(
        StoppedCountCall{"FromDeviceOnADeviceBuffer",
                         [](const TestSupport::SlotBuffers& s) { static_cast<void>(LateCount::from_device(8, s.device)); },
                         "allocate it with Residency::Host"},
        StoppedCountCall{"FromDeviceOnASharedBuffer",
                         [](const TestSupport::SlotBuffers& s) { static_cast<void>(LateCount::from_device(8, s.shared)); },
                         "allocate it with Residency::Host"},
        StoppedCountCall{"FromDeviceOnAnIndexPastTheSlot",
                         [](const TestSupport::SlotBuffers& s)
                         {
                             static_cast<void>(LateCount::from_device(64, s.host, s.host.size()));
                         },
                         "LateCount::from_device: index 4 is out of range for a buffer of 4 elements"}),
    [](const ::testing::TestParamInfo<StoppedCountCall>& info) { return std::string(info.param.name); });

/** @brief Tests that from_device accepts the last index of its slot and keeps its capacity. */
TEST_F(Medium_LateCount, FromDeviceAcceptsTheLastIndexOfItsSlot)
{
    constexpr std::size_t CAPACITY = 64;
    auto slot = runtime().buffer<std::uint32_t>(3, DeviceIndex{0}, Residency::Host).value();

    const LateCount last = LateCount::from_device(CAPACITY, slot, slot.size() - 1);

    EXPECT_EQ(last.capacity(), CAPACITY);
}

/** @brief Tests that from_host stops on a provider that holds no callable. */
TEST(Medium_LateCountEmptyProviderDeathTest, FromHostStopsOnAnEmptyProvider)
{
    EXPECT_DEATH(static_cast<void>(LateCount::from_host(8, LateCount::Provider{})),
                 "LateCount::from_host: the provider is empty");
}

/** @brief Holds one factory call over a capacity a uint32 cannot hold, and the text the stop names. */
struct OverlargeCapacityCall
{
    const char* name;                              /**< Names the row. */
    void (*call)(const TestSupport::SlotBuffers&); /**< Makes the call that stops. */
    std::string pattern;                           /**< What the stop message must match. */
};

/** @brief Returns a capacity one past what a uint32 live-count word holds. */
constexpr std::size_t past_a_uint32()
{
    return static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + std::size_t{1};
}

/** @brief Fixture of the capacity death tests; the parameter is the call, and a target with a narrow size_t skips. */
class Medium_LateCountCapacityDeathTest : public TestSupport::RuntimeTest,
                                          public ::testing::WithParamInterface<OverlargeCapacityCall>
{
    protected:
        /** @brief Skips on a target whose size_t cannot hold a count past a uint32, and allocates the slots otherwise. */
        void SetUp() override
        {
            if (sizeof(std::size_t) <= sizeof(std::uint32_t))
                GTEST_SKIP() << "std::size_t cannot hold a count past a uint32 on this target";
            slots_.emplace(TestSupport::make_slot_buffers(runtime()));
        }

        /** @brief Drops the buffers. */
        void TearDown() override { slots_.reset(); }

        /** @brief Returns the buffers of this test. */
        const TestSupport::SlotBuffers& slots() const { return *slots_; }

    private:
        /** @brief The buffers of this test. */
        std::optional<TestSupport::SlotBuffers> slots_;
};

/** @brief Tests that a factory stops on a capacity a uint32 live-count word cannot hold. */
TEST_P(Medium_LateCountCapacityDeathTest, AFactoryStopsOnACapacityPastAUint32)
{
    EXPECT_DEATH(GetParam().call(slots()), GetParam().pattern);
}

INSTANTIATE_TEST_SUITE_P(
    Factories, Medium_LateCountCapacityDeathTest,
    ::testing::Values(
        OverlargeCapacityCall{"Fixed",
                              [](const TestSupport::SlotBuffers&) { static_cast<void>(LateCount::fixed(past_a_uint32())); },
                              "LateCount::fixed: capacity " + std::to_string(past_a_uint32()) + " exceeds"},
        OverlargeCapacityCall{"FromHost",
                              [](const TestSupport::SlotBuffers&)
                              {
                                  static_cast<void>(
                                      LateCount::from_host(past_a_uint32(), [] { return std::size_t{0}; }));
                              },
                              "LateCount::from_host: capacity " + std::to_string(past_a_uint32()) + " exceeds"},
        OverlargeCapacityCall{"FromDevice",
                              [](const TestSupport::SlotBuffers& s)
                              {
                                  static_cast<void>(LateCount::from_device(past_a_uint32(), s.host));
                              },
                              "LateCount::from_device: capacity " + std::to_string(past_a_uint32()) +
                                  " exceeds"}),
    [](const ::testing::TestParamInfo<OverlargeCapacityCall>& info) { return std::string(info.param.name); });

/** @brief Tests that fixed() accepts the largest capacity a uint32 holds. */
TEST(Small_LateCount, FixedAcceptsTheLargestCapacityAUint32Holds)
{
    const std::size_t limit = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(LateCount::fixed(limit).capacity(), limit);
}

/** @brief Fixture of the vocabulary tests, which run on the program's runtime. */
class Medium_LateBoundVocabulary : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a recorded copy still polls the provider after the original is destroyed. */
TEST_F(Medium_LateBoundVocabulary, ARecordedCopyKeepsTheHostProvider)
{
    auto out = runtime().buffer<std::uint32_t>(1).value();
    out[0] = K_CANARY;
    std::size_t live = 11;
    auto g = runtime().graph();
    {
        const LateCount original = LateCount::from_host(100, [&live] { return live; });
        const LateCount copy = original;
        record_live_count_copy(g.recorder(), copy, out);
    }
    g.run().value();
    live = 93;

    g.run().value();

    EXPECT_EQ(out[0], 93u) << "the copy must poll the provider on every step";
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that from_device reads the word its index names, not the first word. */
TEST_F(Medium_LateBoundVocabulary, FromDeviceReadsTheWordAtItsIndex)
{
    auto slot = runtime().buffer<std::uint32_t>(4, DeviceIndex{0}, Residency::Host).value();
    auto out  = runtime().buffer<std::uint32_t>(1).value();
    TestSupport::assign_from(slot, std::vector<std::uint32_t>{5, 5, 77, 5});
    out[0] = K_CANARY;
    auto g = runtime().graph();
    record_live_count_copy(g.recorder(), LateCount::from_device(100, slot, 2), out);

    g.run().value();

    EXPECT_EQ(out[0], 77u);
}

/** @brief Tests that sized() set after sized_from_device() decides the live size. */
TEST_F(Medium_LateBoundVocabulary, ASizeSetAfterADeviceSizeWins)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::uint32_t DEVICE_COUNT = 3;
    constexpr std::size_t HOST_COUNT = 5;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks   = runtime().buffer<int>(CAPACITY).value();
    counter[0] = DEVICE_COUNT;
    TestSupport::fill_with(marks, 0);

    EXPECT_EQ(count_elements_run(runtime(), sized_from_device(counter).sized([] { return HOST_COUNT; }),
                                 marks, CAPACITY),
              HOST_COUNT)
        << "sized() after sized_from_device() must drop the device slot";
}

/** @brief Tests that sized_from_device() set after sized() decides the live size. */
TEST_F(Medium_LateBoundVocabulary, ADeviceSizeSetAfterASizeWins)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::uint32_t DEVICE_COUNT = 3;
    constexpr std::size_t HOST_COUNT = 5;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks   = runtime().buffer<int>(CAPACITY).value();
    counter[0] = DEVICE_COUNT;
    TestSupport::fill_with(marks, 0);

    EXPECT_EQ(count_elements_run(runtime(), sized([] { return HOST_COUNT; }).sized_from_device(counter),
                                 marks, CAPACITY),
              std::size_t{DEVICE_COUNT})
        << "sized_from_device() after sized() must drop the host provider";
}

/** @brief Tests that the members chain on a named Dynamic and carry all three controls. */
TEST_F(Medium_LateBoundVocabulary, MembersChainOnANamedDynamic)
{
    constexpr std::size_t CAPACITY = 32;
    auto buf = runtime().buffer<int>(CAPACITY).value();
    int* p = buf.data();
    TestSupport::fill_with(buf, 0);
    Dynamic window;
    window.when([] { return true; })
        .sized([] { return std::size_t{4}; })
        .based_at([] { return std::size_t{10}; });
    auto g = runtime().graph();
    g.add_dynamic(std::move(window), CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), TestSupport::window_of(CAPACITY, 10, 4, 1, 0));
}

/** @brief Tests that a Dynamic disabled through its member runs no element. */
TEST_F(Medium_LateBoundVocabulary, ADisabledNamedDynamicRunsNoElement)
{
    constexpr std::size_t CAPACITY = 32;
    auto buf = runtime().buffer<int>(CAPACITY).value();
    int* p = buf.data();
    TestSupport::fill_with(buf, 0);
    Dynamic window;
    window.when([] { return false; }).sized([] { return std::size_t{4}; });
    auto g = runtime().graph();
    g.add_dynamic(std::move(window), CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), std::vector<int>(CAPACITY, 0));
}
