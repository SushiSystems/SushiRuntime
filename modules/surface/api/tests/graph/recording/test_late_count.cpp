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
 * @file test_late_count.cpp
 * @brief Tests where Recorder::resolve() takes a LateCount's live value from, and when.
 * @see modules/surface/api/tests/graph/recording/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    constexpr std::uint32_t K_CANARY = 0xDEADBEEFu;

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
} // namespace

/** @brief Tests that a fixed LateCount resolves to the count it was built with. */
TEST(Integration_LateCount, FixedResolvesToItsCount)
{
    auto rt  = Runtime::create().value();
    auto out = rt.buffer<std::uint32_t>(1).value();
    out[0] = K_CANARY;

    const LateCount count = LateCount::fixed(42);

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), count, out);
    g.run().value();

    EXPECT_EQ(out[0], 42u);
}

/** @brief Tests that a host provider below capacity is the live count. */
TEST(Integration_LateCount, HostProviderBelowCapacity)
{
    auto rt  = Runtime::create().value();
    auto out = rt.buffer<std::uint32_t>(1).value();
    out[0] = K_CANARY;

    const LateCount count =
        LateCount::from_host(1000000, []() { return std::size_t{700000}; });

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), count, out);
    g.run().value();

    EXPECT_EQ(out[0], 700000u);
}

/** @brief Tests that a host count that changes between runs keeps one compile. */
TEST(Integration_LateCount, ChangingHostCountKeepsOneCompile)
{
    auto rt  = Runtime::create().value();
    auto out = rt.buffer<std::uint32_t>(1).value();
    out[0] = K_CANARY;

    std::size_t live = 1000;
    const LateCount count = LateCount::from_host(700000, [&live]() { return live; });

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), count, out);

    g.run().value();
    EXPECT_EQ(out[0], 1000u);

    live = 700000;
    g.run().value();
    EXPECT_EQ(out[0], 700000u);

    live = 0;
    g.run().value();
    EXPECT_EQ(out[0], 0u);

    EXPECT_EQ(g.compile_count(), 1u)
        << "a live count change between runs must not trigger a recompile";
}

/** @brief Tests that a host count above its capacity fails the run and a later run goes on. */
TEST(Integration_LateCount, HostCountAboveCapacityFailsTheRunWithInvalidGraph)
{
    auto rt  = Runtime::create().value();
    auto out = rt.buffer<std::uint32_t>(1).value();
    out[0] = K_CANARY;

    std::size_t live = 101;
    const LateCount count = LateCount::from_host(100, [&live]() { return live; });

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), count, out);

    const auto failed = g.run();
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidGraph);
    EXPECT_NE(failed.error().message.find("the live count (101) exceeds the capacity"),
              std::string::npos)
        << failed.error().message;
    EXPECT_EQ(out[0], K_CANARY) << "a rejected count must not reach the fill";

    // The process goes on: the same graph runs once the provider returns a count that fits.
    live = 100;
    const auto valid = g.run();
    ASSERT_TRUE(valid.has_value()) << valid.error().message;
    EXPECT_EQ(out[0], 100u);
}

/** @brief Tests that a Residency::Host device slot resolves to the value written there. */
TEST(Integration_LateCount, DeviceSlotResolvesFromHostResidentBuffer)
{
    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = rt.buffer<std::uint32_t>(1).value();
    counter[0] = 700000;
    out[0] = K_CANARY;

    auto g = rt.graph();
    record_live_count_copy(g.recorder(), device_count(counter, 1000000), out);
    g.run().value();

    EXPECT_EQ(out[0], 700000u);
}

/** @brief Tests that a device count above its capacity fails the run on both layers. */
TEST(Integration_LateCount, DeviceCountAboveCapacityFailsTheRunOnBothLayers)
{
    for (const bool force_scheduler : {false, true})
    {
        RuntimeConfig config;
        config.force_scheduler = force_scheduler;
        auto rt      = Runtime::create(config).value();
        auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
        auto out     = rt.buffer<std::uint32_t>(1).value();
        counter[0] = 101;
        out[0] = K_CANARY;

        auto g = rt.graph();
        record_live_count_copy(g.recorder(), device_count(counter, 100), out);
        EXPECT_FALSE(g.run().has_value()) << "force_scheduler = " << force_scheduler;

        // A later graph with a count inside the capacity runs clean on the same runtime.
        counter[0] = 100;
        auto valid = rt.graph();
        record_live_count_copy(valid.recorder(), device_count(counter, 100), out);
        const auto report = valid.run().value();
        EXPECT_TRUE(report.is_successful) << "force_scheduler = " << force_scheduler;
        EXPECT_EQ(out[0], 100u) << "force_scheduler = " << force_scheduler;
    }
}

/** @brief Tests that a device slot outside Residency::Host stops the call that builds the count. */
TEST(Integration_LateCountDeathTest, ANonHostDeviceSlotStopsAtConstruction)
{
    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1).value();

    EXPECT_DEATH(static_cast<void>(LateCount::from_device(16, counter)),
                 "LateCount::from_device: the buffer is Residency::Shared");
}

/** @brief Tests that the resolver sees the count its predecessor wrote in the same run. */
TEST(Integration_LateCount, ResolverSeesTheCountItsPredecessorJustWrote)
{
    constexpr std::size_t CAPACITY = 1000;

    auto rt      = Runtime::create().value();
    auto counter = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto source  = rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value();
    auto out     = rt.buffer<std::uint32_t>(1).value();
    counter[0] = 0;
    out[0] = K_CANARY;

    // A device kernel cannot reach this frame, so its next count is in a buffer; see README.md.
    const std::uint32_t* source_ptr = source.data();
    std::uint32_t* counter_ptr = counter.data();

    auto g = rt.graph();
    g.add(std::size_t{1}, Reads(source), Writes(counter),
          [counter_ptr, source_ptr](std::size_t) { counter_ptr[0] = source_ptr[0]; });
    record_live_count_copy(g.recorder(), device_count(counter, CAPACITY), out);

    source[0] = 300;
    g.run().value();
    EXPECT_EQ(out[0], 300u) << "the resolver must read the count its own step's producer wrote";

    source[0] = 700;
    g.run().value();
    EXPECT_EQ(out[0], 700u) << "a changed count must take effect the same run, not one run late";

    EXPECT_EQ(g.compile_count(), 1u);
}
