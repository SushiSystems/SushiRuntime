/**************************************************************************/
/* test_dynamic.cpp                                                       */
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
 * @file test_dynamic.cpp
 * @brief Tests late-bound sizes, bases, enables and device counts of a dynamic launch.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"
#include "support/slot_buffers.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns @p capacity elements that hold i + @p shift for i in [@p base, @p base + @p count) and -1 elsewhere. */
    std::vector<int> absolute_window(std::size_t capacity, std::size_t base, std::size_t count, int shift)
    {
        std::vector<int> values(capacity, -1);
        for (std::size_t i = base; i < base + count && i < capacity; ++i)
            values[i] = static_cast<int>(i) + shift;
        return values;
    }

    /** @brief Runs @p graph once for each live size from 1 to @p capacity, setting @p live before each run. */
    void run_with_each_live_size(Graph& graph, std::size_t& live, std::size_t capacity)
    {
        for (std::size_t size = 1; size <= capacity; ++size)
        {
            live = size;
            graph.run().value();
        }
    }

    /** @brief Runs @p graph @p steps times, enabled on the even steps only, and returns how many steps were enabled. */
    std::size_t run_enabled_on_even_steps(Graph& graph, bool& on, std::size_t steps)
    {
        std::size_t enabled_steps = 0;
        for (std::size_t k = 0; k < steps; ++k)
        {
            on = (k % 2 == 0);
            enabled_steps += on ? 1u : 0u;
            graph.run().value();
        }
        return enabled_steps;
    }

    /** @brief Runs @p graph for @p steps steps with a live prefix of 2, 4, 6, ... cells. */
    void run_with_a_growing_prefix(Graph& graph, std::size_t& live, std::size_t steps)
    {
        for (std::size_t k = 0; k < steps; ++k)
        {
            live = 2 * (k + 1);
            graph.run().value();
        }
    }

    /**
     * @brief Runs @p graph for @p steps steps, disabled every third step and live 2 on the odd steps and @p capacity on the even ones.
     * @return How many increments element 0 should have seen: the steps that were enabled and live.
     */
    int run_composed(Graph& graph, bool& on, std::size_t& live, std::size_t capacity, std::size_t steps)
    {
        int expected = 0;
        for (std::size_t k = 0; k < steps; ++k)
        {
            on = (k % 3 != 0);
            live = (k % 2 == 0) ? capacity : 2;
            expected += (on && live > 0) ? 1 : 0;
            graph.run().value();
        }
        return expected;
    }
} // namespace

/** @brief Fixture of the late-binding tests, which run on the program's runtime. */
class Medium_LateBindings : public TestSupport::RuntimeTest
{
};

/** @brief Tests that sized() shrinks the iteration range to a live prefix each step. */
TEST_F(Medium_LateBindings, DynamicSizePerElement)
{
    constexpr std::size_t CAP = 8;
    auto buf = runtime().buffer<int>(CAP).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    std::size_t live = 0;
    auto g = runtime().graph();
    g.add_dynamic(sized([&] { return live; }), CAP, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] += 1; });

    run_with_each_live_size(g, live, CAP);

    EXPECT_EQ(g.compile_count(), 1u) << "size changes must not recompile.";
    std::vector<int> expected = TestSupport::ramp<int>(CAP);
    for (int& value : expected)
        value = static_cast<int>(CAP) - value;
    EXPECT_EQ(TestSupport::contents(buf), expected);
}

/** @brief Tests that when() runs the node only on the enabled steps. */
TEST_F(Medium_LateBindings, EnableTogglePerElement)
{
    constexpr std::size_t N     = 16;
    constexpr std::size_t STEPS = 10;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    bool on = true;
    auto g = runtime().graph();
    g.add_dynamic(when([&] { return on; }), N, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] += 1; });

    const std::size_t enabled_steps = run_enabled_on_even_steps(g, on, STEPS);

    EXPECT_EQ(g.compile_count(), 1u) << "enable toggles must not recompile.";
    EXPECT_EQ(TestSupport::contents(buf), std::vector<int>(N, static_cast<int>(enabled_steps)));
}

/** @brief Tests that a disabled node still releases its successors. */
TEST_F(Medium_LateBindings, DisabledNodeStillReleasesSuccessors)
{
    constexpr std::size_t N     = 8;
    constexpr std::size_t STEPS = 6;
    auto gate = runtime().buffer<int>(N).value();   // written by the toggled upstream node
    auto sink = runtime().buffer<int>(N).value();   // written by the always-on downstream node
    TestSupport::fill_with(gate, 0);
    TestSupport::fill_with(sink, 0);
    int* gp = gate.data();
    int* sp = sink.data();
    bool on = false;
    auto g = runtime().graph();
    // Downstream reads the gate buffer upstream writes, so it is ordered after; see README.md.
    g.add_dynamic(when([&] { return on; }), N, Reads{}, Writes(gate), [gp](std::size_t i) { gp[i] += 1; });
    g.add(N, Reads(gate), Writes(sink), [sp](std::size_t i) { sp[i] += 1; });

    g.run(STEPS).value();                       // upstream stays disabled the whole run

    EXPECT_EQ(g.compile_count(), 1u);
    EXPECT_EQ(TestSupport::contents(gate), std::vector<int>(N, 0)) << "disabled upstream must not run";
    EXPECT_EQ(TestSupport::contents(sink), std::vector<int>(N, static_cast<int>(STEPS)))
        << "downstream must run every step";
}

/** @brief Tests that sized() on a stepped State follows a live prefix that grows each step. */
TEST_F(Medium_LateBindings, DynamicSizeState)
{
    constexpr std::size_t CAP   = 8;
    constexpr std::size_t STEPS = 4;
    auto field = runtime().state<int>(CAP).value();
    TestSupport::fill_with(field, 0);
    std::size_t live = 0;
    auto g = runtime().graph();
    g.add_dynamic(sized([&] { return live; }), field,
                  [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Grow the active prefix 2,4,6,8; the first two cells are active every step.
    run_with_a_growing_prefix(g, live, STEPS);

    EXPECT_EQ(g.compile_count(), 1u) << "state size changes must not recompile.";
    EXPECT_EQ(field[0], static_cast<int>(STEPS));
    EXPECT_EQ(field[1], static_cast<int>(STEPS));
}

/** @brief Tests that when() and sized() compose on one node. */
TEST_F(Medium_LateBindings, ComposedWhenAndSized)
{
    constexpr std::size_t CAP   = 4;
    constexpr std::size_t STEPS = 8;
    auto buf = runtime().buffer<int>(CAP).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    bool on = true;
    std::size_t live = CAP;
    auto g = runtime().graph();
    g.add_dynamic(when([&] { return on; }).sized([&] { return live; }), CAP, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] += 1; });

    const int expected_first = run_composed(g, on, live, CAP, STEPS);

    EXPECT_EQ(g.compile_count(), 1u);
    EXPECT_EQ(buf[0], expected_first);
}

/** @brief Holds one window of the slice test: where it starts and how long it is. */
struct SliceWindow
{
    std::size_t base;  /**< First element the node runs. */
    std::size_t count; /**< How many elements it runs. */
};

/** @brief Fixture of the slice tests; the parameter is the window. */
class Medium_LateBindingsSlice : public TestSupport::RuntimeTest,
                                 public ::testing::WithParamInterface<SliceWindow>
{
};

/** @brief Tests that based_at() with sized() runs exactly the window, passing absolute indices. */
TEST_P(Medium_LateBindingsSlice, BaseOffsetIteratesASlice)
{
    constexpr std::size_t CAPACITY = 64;
    auto buf = runtime().buffer<int>(CAPACITY).value();
    TestSupport::fill_with(buf, -1);
    int* p = buf.data();
    const SliceWindow window = GetParam();
    auto g = runtime().graph();
    g.add_dynamic(based_at([window] { return window.base; }).sized([window] { return window.count; }),
                  CAPACITY, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] = static_cast<int>(i) + 1000; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), absolute_window(CAPACITY, window.base, window.count, 1000));
}

INSTANTIATE_TEST_SUITE_P(Windows, Medium_LateBindingsSlice,
                         ::testing::Values(SliceWindow{0, 4}, SliceWindow{8, 16}, SliceWindow{40, 20},
                                           SliceWindow{63, 1}));

/** @brief Tests that moving the window between runs does not recompile. */
TEST_F(Medium_LateBindings, MovingTheWindowDoesNotRecompile)
{
    constexpr std::size_t CAPACITY = 64;
    auto buf = runtime().buffer<int>(CAPACITY).value();
    int* p = buf.data();
    std::size_t base = 0;
    std::size_t count = 4;
    auto g = runtime().graph();
    g.add_dynamic(based_at([&base] { return base; }).sized([&count] { return count; }),
                  CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.run().value();
    base = 8;
    count = 16;

    g.run().value();

    EXPECT_EQ(g.compile_count(), 1u) << "moving the window must not recompile";
}

/**
 * @brief Tests that based_at_device() and sized_from_device() take both values from slots a
 *        predecessor wrote.
 */
TEST_F(Medium_LateBindings, BaseAndSizeBothFromTheDevice)
{
    constexpr std::size_t CAPACITY = 128;
    // slice[0] is the base and slice[1] the count.
    auto slice = runtime().buffer<std::uint32_t>(2, DeviceIndex{0}, Residency::Host).value();
    auto buf   = runtime().buffer<int>(CAPACITY).value();
    std::uint32_t* s = slice.data();
    int* p = buf.data();
    TestSupport::fill_with(buf, -1);
    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads{}, Writes(slice), [s](std::size_t) { s[0] = 30u; s[1] = 25u; });
    g.add_dynamic(based_at_device(slice, 0).sized_from_device(slice, 1), CAPACITY, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] = static_cast<int>(i); });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), absolute_window(CAPACITY, 30, 25, 0));
}

/** @brief Tests that a window that fits runs after a run whose window ran past the capacity failed. */
TEST_F(Medium_LateBindings, ALaterWindowThatFitsRunsAfterOnePastTheCapacityFailed)
{
    constexpr std::size_t CAPACITY = 16;
    auto buf = runtime().buffer<int>(CAPACITY).value();
    int* p = buf.data();
    std::size_t base = 10;
    auto g = runtime().graph();
    g.add_dynamic(based_at([&base] { return base; }).sized([] { return std::size_t{10}; }),
                  CAPACITY, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = 1; });
    static_cast<void>(g.run());     // 10 elements from index 10 does not fit 16
    base = 6;

    EXPECT_TRUE(g.run().has_value());
}

/** @brief Holds one call that must stop the process because a slot cannot be read, and the text the stop names. */
struct StoppedSlotCall
{
    const char* name;                              /**< Names the row. */
    void (*call)(const TestSupport::SlotBuffers&); /**< Makes the call that stops. */
    const char* pattern;                           /**< What the stop message must match. */
};

/** @brief Fixture of the slot death tests; the parameter is the call. */
class Medium_LateBindingsDeathTest : public TestSupport::RuntimeTest,
                                     public ::testing::WithParamInterface<StoppedSlotCall>
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

/** @brief Tests that a call that names a slot the host cannot read stops the process at the call. */
TEST_P(Medium_LateBindingsDeathTest, ASlotTheHostCannotReadStopsTheCallThatNamesIt)
{
    EXPECT_DEATH(GetParam().call(slots()), GetParam().pattern);
}

INSTANTIATE_TEST_SUITE_P(
    Calls, Medium_LateBindingsDeathTest,
    ::testing::Values(
        StoppedSlotCall{"BasedAtDeviceOnADeviceBuffer",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(based_at_device(s.device)); },
                        "based_at_device: the buffer is Residency::Device"},
        StoppedSlotCall{"BasedAtDeviceOnASharedBuffer",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(based_at_device(s.shared)); },
                        "based_at_device: the buffer is Residency::Shared"},
        StoppedSlotCall{"BasedAtDeviceOnAnIndexPastTheSlot",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(based_at_device(s.host, 7)); },
                        "based_at_device: index 7 is out of range"},
        StoppedSlotCall{"SizedFromDeviceOnADeviceBuffer",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(sized_from_device(s.device)); },
                        "sized_from_device: the buffer is Residency::Device.*Residency::Host"},
        StoppedSlotCall{"SizedFromDeviceOnASharedBuffer",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(sized_from_device(s.shared)); },
                        "sized_from_device: the buffer is Residency::Shared.*Residency::Host"},
        StoppedSlotCall{"SizedFromDeviceOnAnIndexPastTheSlot",
                        [](const TestSupport::SlotBuffers& s) { static_cast<void>(sized_from_device(s.host, 5)); },
                        "sized_from_device: index 5 is out of range"},
        StoppedSlotCall{"TheMemberSizedFromDeviceOnADeviceBuffer",
                        [](const TestSupport::SlotBuffers& s)
                        {
                            Dynamic dyn;
                            static_cast<void>(dyn.sized_from_device(s.device, 1));
                        },
                        "sized_from_device: the buffer is Residency::Device"},
        StoppedSlotCall{"TheMemberSizedFromDeviceOnASharedBuffer",
                        [](const TestSupport::SlotBuffers& s)
                        {
                            Dynamic dyn;
                            static_cast<void>(dyn.sized_from_device(s.shared, 1));
                        },
                        "sized_from_device: the buffer is Residency::Shared"},
        StoppedSlotCall{"TheMemberBasedAtDeviceOnAnIndexPastTheSlot",
                        [](const TestSupport::SlotBuffers& s)
                        {
                            Dynamic dyn;
                            static_cast<void>(dyn.based_at_device(s.host, 4));
                        },
                        "based_at_device: index 4 is out of range"}),
    [](const ::testing::TestParamInfo<StoppedSlotCall>& info) { return std::string(info.param.name); });

/** @brief Fixture of the device count tests, which run on the program's runtime. */
class Medium_DeviceCount : public TestSupport::RuntimeTest
{
};

/** @brief Holds one multiplier of the count a producer writes. */
struct CountMultiplier
{
    std::uint32_t multiplier; /**< The producer writes three times this. */
};

/** @brief Fixture of the producer-consumer tests; the parameter is the multiplier. */
class Medium_DeviceCountProducer : public TestSupport::RuntimeTest,
                                   public ::testing::WithParamInterface<CountMultiplier>
{
};

/** @brief Tests that a consumer iterates exactly the count the producer wrote in the same run(). */
TEST_P(Medium_DeviceCountProducer, ConsumerIteratesTheCountTheProducerJustWrote)
{
    constexpr std::size_t CAPACITY = 4096;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto marks   = runtime().buffer<int>(CAPACITY).value();
    // The producer reads its input through a pointer, so the device computes the count.
    auto wanted  = runtime().buffer<std::uint32_t>(1).value();
    std::uint32_t* c = counter.data();
    std::uint32_t* w = wanted.data();
    int* m = marks.data();
    counter[0] = 0;
    wanted[0] = GetParam().multiplier;
    TestSupport::fill_with(marks, -1);
    auto g = runtime().graph();
    // Producer: derive the count on the device from data the kernel reads.
    g.add(std::size_t{1}, Reads(wanted), Writes(counter),
          [c, w](std::size_t) { c[0] = w[0] * 3u; });

    // Consumer: iterate exactly that many elements.
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(marks),
                  [m](std::size_t i) { m[i] = static_cast<int>(i); });

    g.run().value();

    EXPECT_EQ(counter[0], GetParam().multiplier * 3u);
    EXPECT_EQ(TestSupport::contents(marks),
              absolute_window(CAPACITY, 0, static_cast<std::size_t>(GetParam().multiplier) * 3u, 0));
    EXPECT_EQ(g.compile_count(), 1u) << "a device-driven count must not force a recompile";
}

INSTANTIATE_TEST_SUITE_P(Multipliers, Medium_DeviceCountProducer,
                         ::testing::Values(CountMultiplier{5}, CountMultiplier{200}, CountMultiplier{1},
                                           CountMultiplier{0}, CountMultiplier{1000}));

/** @brief Tests that a count that changes between runs does not recompile. */
TEST_F(Medium_DeviceCount, ChangingTheDeviceCountKeepsOneCompile)
{
    constexpr std::size_t CAPACITY = 64;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = runtime().buffer<int>(CAPACITY).value();
    int* o = out.data();
    auto g = runtime().graph();
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(out), [o](std::size_t i) { o[i] = 1; });
    counter[0] = 10;
    g.run().value();
    counter[0] = 40;

    g.run().value();

    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests a three-stage chain where each stage's output count sizes the next stage. */
TEST_F(Medium_DeviceCount, ChainedProducerCountsInOneComposition)
{
    constexpr std::size_t CAPACITY = 1024;
    auto first  = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto second = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto stage1 = runtime().buffer<int>(CAPACITY).value();
    auto stage2 = runtime().buffer<int>(CAPACITY).value();
    std::uint32_t* c1 = first.data();
    std::uint32_t* c2 = second.data();
    int* s1 = stage1.data();
    int* s2 = stage2.data();
    first[0] = 0;
    second[0] = 0;
    TestSupport::fill_with(stage1, 0);
    TestSupport::fill_with(stage2, 0);
    auto g = runtime().graph();
    // Stage 0: pick a first-level count on the device.
    g.add(std::size_t{1}, Reads(), Writes(first), [c1](std::size_t) { c1[0] = 700u; });
    // Stage 1: iterate it, and produce the next count from how much it did.
    g.add_dynamic(sized_from_device(first), CAPACITY, Reads(), Writes(stage1),
                  [s1](std::size_t i) { s1[i] = 1; });
    g.add(std::size_t{1}, Reads(first), Writes(second), [c1, c2](std::size_t) { c2[0] = c1[0] / 2u; });
    // Stage 2: iterate the second count.
    g.add_dynamic(sized_from_device(second), CAPACITY, Reads(), Writes(stage2),
                  [s2](std::size_t i) { s2[i] = 1; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(stage1), TestSupport::window_of(CAPACITY, 0, 700, 1, 0));
    EXPECT_EQ(TestSupport::contents(stage2), TestSupport::window_of(CAPACITY, 0, 350, 1, 0));
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Fixture of the when() with device size tests: a producer writes 100 and the consumer adds one per live element. */
class Medium_DeviceCountWhen : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Records the producer and the consumer gated by enabled_. */
        void SetUp() override
        {
            counter_.emplace(runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value());
            out_.emplace(runtime().buffer<int>(K_CAPACITY).value());
            std::uint32_t* c = counter_->data();
            int* o = out_->data();
            (*counter_)[0] = 0;
            TestSupport::fill_with(*out_, 0);
            graph_.emplace(runtime().graph());
            graph_->add(std::size_t{1}, Reads(), Writes(*counter_), [c](std::size_t) { c[0] = 100u; });
            graph_->add_dynamic(sized_from_device(*counter_).when([this] { return enabled_; }), K_CAPACITY,
                                Reads(), Writes(*out_), [o](std::size_t i) { o[i] += 1; });
        }

        /** @brief Drops the graph before the buffers it records. */
        void TearDown() override
        {
            graph_.reset();
            out_.reset();
            counter_.reset();
        }

        /** @brief Runs the graph once with the consumer enabled or not. */
        void run_once(bool enabled)
        {
            enabled_ = enabled;
            graph_->run().value();
        }

        /** @brief How many elements the consumer can run. */
        static constexpr std::size_t K_CAPACITY = 256;

        /** @brief Whether the consumer is enabled. */
        bool enabled_ = true;

        /** @brief The count the producer writes. */
        std::optional<Buffer<std::uint32_t>> counter_;

        /** @brief The buffer the consumer increments. */
        std::optional<Buffer<int>> out_;

        /** @brief The recorded graph. */
        std::optional<Graph> graph_;
};

/** @brief Tests that an enabled step runs the live count. */
TEST_F(Medium_DeviceCountWhen, AnEnabledStepRunsTheLiveCount)
{
    run_once(true);

    EXPECT_EQ(TestSupport::contents(*out_), TestSupport::window_of(K_CAPACITY, 0, 100, 1, 0));
}

/** @brief Tests that a disabled step runs no element whatever the counter says. */
TEST_F(Medium_DeviceCountWhen, ADisabledStepRunsNoElement)
{
    run_once(false);

    EXPECT_EQ(TestSupport::contents(*out_), std::vector<int>(K_CAPACITY, 0));
}

/** @brief Tests that a step enabled again after a disabled one runs the live count again. */
TEST_F(Medium_DeviceCountWhen, AStepEnabledAgainRunsTheLiveCount)
{
    run_once(true);
    run_once(false);

    run_once(true);

    EXPECT_EQ(TestSupport::contents(*out_), TestSupport::window_of(K_CAPACITY, 0, 100, 2, 0));
}

/** @brief Tests that a count above the compiled capacity fails the run. */
TEST_F(Medium_DeviceCount, CountOverCapacityFailsTheRun)
{
    constexpr std::size_t CAPACITY = 64;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = runtime().buffer<int>(CAPACITY).value();
    std::uint32_t* c = counter.data();
    int* o = out.data();
    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads(), Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(out), [o](std::size_t i) { o[i] = 1; });

    EXPECT_FALSE(g.run().has_value());
}

/** @brief Tests that a clean graph runs on the same runtime after a run failed for a count over the capacity. */
TEST_F(Medium_DeviceCount, ACleanGraphRunsAfterACountOverCapacityFailedARun)
{
    constexpr std::size_t CAPACITY = 64;
    auto counter = runtime().buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = runtime().buffer<int>(CAPACITY).value();
    std::uint32_t* c = counter.data();
    int* o = out.data();
    auto g = runtime().graph();
    g.add(std::size_t{1}, Reads(), Writes(counter),
          [c](std::size_t) { c[0] = static_cast<std::uint32_t>(CAPACITY) + 1u; });
    g.add_dynamic(sized_from_device(counter), CAPACITY, Reads(), Writes(out), [o](std::size_t i) { o[i] = 1; });
    static_cast<void>(g.run());
    auto clean = runtime().graph();
    clean.add(CAPACITY, Reads(), Writes(out), [o](std::size_t i) { o[i] = 7; });

    clean.run().value();

    EXPECT_EQ(TestSupport::contents(out), std::vector<int>(CAPACITY, 7));
}

/** @brief Tests that a count is read from the element the index names, not from element zero. */
TEST_F(Medium_DeviceCount, ReadsTheCountAtTheGivenIndex)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 5;
    auto plan = runtime().buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host).value();
    auto out  = runtime().buffer<int>(CAPACITY).value();
    TestSupport::fill_with(out, -1);
    plan[SLOT] = 3;
    int* o = out.data();
    auto g = runtime().graph();
    g.add_dynamic(sized_from_device(plan, SLOT), CAPACITY, Reads(), Writes(out),
                  [o](std::size_t i) { o[i] = static_cast<int>(i); });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(out), absolute_window(CAPACITY, 0, 3, 0));
}

/** @brief Tests that a base is read from the element the index names, and the node runs from there to the end. */
TEST_F(Medium_DeviceCount, AcceptsABaseSlotAtTheGivenIndex)
{
    constexpr std::size_t CAPACITY = 16;
    constexpr std::size_t SLOT     = 3;
    auto plan = runtime().buffer<std::uint32_t>(8, DeviceIndex{0}, Residency::Host).value();
    auto out  = runtime().buffer<int>(CAPACITY).value();
    TestSupport::fill_with(out, -1);
    plan[SLOT] = 4;
    int* o = out.data();
    auto g = runtime().graph();
    g.add_dynamic(based_at_device(plan, SLOT), CAPACITY, Reads(), Writes(out),
                  [o](std::size_t i) { o[i] = static_cast<int>(i); });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(out), absolute_window(CAPACITY, 4, CAPACITY - 4, 0));
}

namespace
{
    /** @brief Names the element type of every handle of the slot liveness tests. */
    using Word = std::uint32_t;
} // namespace

/** @brief Names which buffer of a recorded Dynamic the liveness test destroys. */
enum class WatchedSlot : std::uint8_t
{
    CountOfATask, /**< The buffer sized_from_device() of a task reads. */
    BaseOfATask,  /**< The buffer based_at_device() of a task reads. */
    CountOfAState /**< The buffer sized_from_device() of a State rule reads. */
};

/** @brief Fixture of the slot liveness tests; the parameter names the slot. */
class Medium_DeviceSlotLiveness : public TestSupport::RuntimeTest,
                                  public ::testing::WithParamInterface<WatchedSlot>
{
    protected:
        /** @brief Width of the target of every recorded node. */
        static constexpr std::size_t K_WIDTH = 64;

        /** @brief Allocates a one-word Residency::Host buffer that holds @p value. */
        Buffer<Word> host_slot(Word value)
        {
            auto slot = runtime().buffer<Word>(1, DeviceIndex{0}, Residency::Host).value();
            slot[0] = value;
            return slot;
        }

        /** @brief Records into @p graph a node that reads @p slot as the row says. */
        void record_reading(Graph& graph, Buffer<Word>& target, State<Word>& field, Buffer<Word>& slot)
        {
            Word* words = target.data();
            if (GetParam() == WatchedSlot::BaseOfATask)
                graph.add_dynamic(based_at_device(slot), K_WIDTH, Reads(), Writes(target),
                                  [words](std::size_t i) { words[i] = 1u; });
            else if (GetParam() == WatchedSlot::CountOfATask)
                graph.add_dynamic(sized_from_device(slot, 0), K_WIDTH, Reads(), Writes(target),
                                  [words](std::size_t i) { words[i] = 1u; });
            else
                graph.add_dynamic(sized_from_device(slot), field,
                                  [](std::size_t i, const Word* current, Word* next) { next[i] = current[i]; });
        }
};

/** @brief Tests that a run is refused with DanglingResource once the slot buffer is destroyed. */
TEST_P(Medium_DeviceSlotLiveness, ADestroyedSlotBufferRefusesTheRun)
{
    auto target = runtime().buffer<Word>(K_WIDTH).value();
    auto field = runtime().state<Word>(K_WIDTH).value();
    std::optional<Buffer<Word>> slot(host_slot(24));
    auto graph = runtime().graph();
    record_reading(graph, target, field, *slot);
    ASSERT_TRUE(graph.run());
    slot.reset();

    auto refused = graph.run();

    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
}

INSTANTIATE_TEST_SUITE_P(Slots, Medium_DeviceSlotLiveness,
                         ::testing::Values(WatchedSlot::CountOfATask, WatchedSlot::BaseOfATask,
                                           WatchedSlot::CountOfAState),
                         [](const ::testing::TestParamInfo<WatchedSlot>& info)
                         {
                             switch (info.param)
                             {
                                 case WatchedSlot::CountOfATask:  return std::string("CountOfATask");
                                 case WatchedSlot::BaseOfATask:   return std::string("BaseOfATask");
                                 case WatchedSlot::CountOfAState: return std::string("CountOfAState");
                             }
                             return std::string("Unknown");
                         });

/** @brief Fixture of the recorder test, which runs on the program's runtime. */
class Medium_DeviceSlotRecorder : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the recorder watches the target and the counter of a dynamic task. */
TEST_F(Medium_DeviceSlotRecorder, TheRecorderWatchesTheTargetAndTheCounter)
{
    constexpr std::size_t K_WIDTH = 64;
    auto target = runtime().buffer<Word>(K_WIDTH).value();
    auto counter = runtime().buffer<Word>(1, DeviceIndex{0}, Residency::Host).value();
    counter[0] = 24;
    Word* words = target.data();
    auto graph = runtime().graph();

    graph.add_dynamic(sized_from_device(counter, 0), K_WIDTH, Reads(), Writes(target),
                      [words](std::size_t i) { words[i] = 1u; });

    EXPECT_EQ(graph.recorder().watched_allocation_count(), 2u);
}
