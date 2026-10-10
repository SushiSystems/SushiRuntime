/**************************************************************************/
/* test_recorder_scratch.cpp                                              */
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
 * @file test_recorder_scratch.cpp
 * @brief Tests the USM a Recorder hands out to a rule: its lifetime, replay and regions.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t COUNT = 1000;

    /** @brief Returns what the staged rule writes for one input value. */
    std::uint32_t staged(std::uint32_t value)
    {
        return (value + 1u) * 2u;
    }

    /**
     * @brief Records one rule of two nodes that hand their values through recorder scratch.
     *
     * The first node writes in[i] + 1 into scratch and the second writes twice that into out.
     * @param in  Holds at least COUNT values; must outlive the recorder.
     * @param out Holds at least COUNT values; must outlive the recorder.
     */
    void record_staged_rule(API::Recorder& recorder, const API::Buffer<std::uint32_t>& in,
                            const API::Buffer<std::uint32_t>& out)
    {
        std::uint32_t* stage =
            recorder.scratch<std::uint32_t>(COUNT, in.device_index(), in.residency());
        const Core::ResourceRegion stage_region{stage, 0, COUNT * sizeof(std::uint32_t)};
        const std::uint32_t* source = in.data();
        std::uint32_t* target = out.data();

        auto rule = recorder.rule();
        recorder.kernel(COUNT, API::Access(Reads(in), Writes(stage_region)),
                        [source, stage](std::size_t i) { stage[i] = source[i] + 1u; });
        recorder.kernel(COUNT, API::Access(Reads(stage_region), Writes(out)),
                        [stage, target](std::size_t i) { target[i] = stage[i] * 2u; });
    }

    /** @brief Fills a buffer with a pattern that differs per seed. */
    void fill(API::Buffer<std::uint32_t>& buffer, std::uint32_t seed)
    {
        for (std::size_t i = 0; i < COUNT; ++i)
            buffer[i] = static_cast<std::uint32_t>(i) * 3u + seed;
    }
} // namespace

/** @brief Tests that a consumer of a rule's output sees what passed through its scratch. */
TEST(Integration_RecorderScratch, AConsumerSeesWhatPassedThroughTheScratch)
{
    auto rt   = Runtime::create();
    auto in   = rt.buffer<std::uint32_t>(COUNT);
    auto out  = rt.buffer<std::uint32_t>(COUNT);
    auto copy = rt.buffer<std::uint32_t>(COUNT);
    fill(in, 5);
    for (std::size_t i = 0; i < COUNT; ++i)
    {
        out[i] = 0;
        copy[i] = 0;
    }
    const std::uint32_t* o = out.data();
    std::uint32_t* c = copy.data();

    auto g = rt.graph();
    record_staged_rule(g.recorder(), in, out);
    g.add(COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i] + 1u; });
    g.run();

    for (std::size_t i = 0; i < COUNT; ++i)
    {
        ASSERT_EQ(out[i], staged(in[i])) << "i = " << i;
        ASSERT_EQ(copy[i], staged(in[i]) + 1u) << "i = " << i;
    }
}

/** @brief Tests that each run of a replayed rule reads its input as it stands, in one compile. */
TEST(Integration_RecorderScratch, EachRunReadsItsInputAsItStands)
{
    auto rt  = Runtime::create();
    auto in  = rt.buffer<std::uint32_t>(COUNT);
    auto out = rt.buffer<std::uint32_t>(COUNT);

    auto g = rt.graph();
    record_staged_rule(g.recorder(), in, out);

    for (std::uint32_t step = 0; step < 4; ++step)
    {
        fill(in, step * 11u);
        g.run();
        for (std::size_t i = 0; i < COUNT; ++i)
            ASSERT_EQ(out[i], staged(in[i])) << "step " << step << ", i = " << i;
    }
    EXPECT_EQ(g.compile_count(), 1u);
}

/** @brief Tests that the scratch a kernel captured stays valid after the graph recompiles. */
TEST(Integration_RecorderScratch, ScratchSurvivesARecompile)
{
    auto rt   = Runtime::create();
    auto in   = rt.buffer<std::uint32_t>(COUNT);
    auto out  = rt.buffer<std::uint32_t>(COUNT);
    auto copy = rt.buffer<std::uint32_t>(COUNT);
    fill(in, 1);
    const std::uint32_t* o = out.data();
    std::uint32_t* c = copy.data();

    auto g = rt.graph();
    record_staged_rule(g.recorder(), in, out);
    g.run();
    for (std::size_t i = 0; i < COUNT; ++i)
        ASSERT_EQ(out[i], staged(in[i])) << "first plan, i = " << i;

    // A node recorded after a run makes the next run build a second plan.
    g.add(COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i]; });
    fill(in, 9);
    g.run();

    EXPECT_EQ(g.compile_count(), 2u);
    for (std::size_t i = 0; i < COUNT; ++i)
    {
        ASSERT_EQ(out[i], staged(in[i])) << "second plan, i = " << i;
        ASSERT_EQ(copy[i], staged(in[i])) << "second plan, i = " << i;
    }
}

/** @brief Tests that scratch returns to the runtime when the graph that recorded it dies. */
TEST(Integration_RecorderScratch, ScratchIsFreedWithTheRecorder)
{
    auto rt  = Runtime::create();
    auto in  = rt.buffer<std::uint32_t>(COUNT);
    auto out = rt.buffer<std::uint32_t>(COUNT);
    const Memory::UsmAllocator& memory = rt.unsafe_context().memory();
    const std::size_t baseline = memory.live_count();

    {
        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);
        EXPECT_EQ(memory.live_count(), baseline + 1u) << "the rule takes one scratch allocation";
    }

    EXPECT_EQ(memory.live_count(), baseline);
}

/** @brief Tests that regions record rules with scratch and keep it across a region rebuild. */
TEST(Integration_RecorderScratch, RegionsRecordRulesWithScratch)
{
    auto rt  = Runtime::create();
    auto in  = rt.buffer<std::uint32_t>(COUNT);
    auto mid = rt.buffer<std::uint32_t>(COUNT);
    auto out = rt.buffer<std::uint32_t>(COUNT);

    auto world = rt.dynamic_graph();
    record_staged_rule(world.region(1).recorder(), in, mid);
    record_staged_rule(world.region(2).recorder(), mid, out);

    for (std::uint32_t round = 0; round < 3; ++round)
    {
        fill(in, round * 7u);
        // Renaming a rule changes the recording, so the last round replays region 1.
        if (round == 2)
            world.region(1).name("staged");
        world.run();
        for (std::size_t i = 0; i < COUNT; ++i)
        {
            ASSERT_EQ(mid[i], staged(in[i])) << "round " << round << ", i = " << i;
            ASSERT_EQ(out[i], staged(staged(in[i]))) << "round " << round << ", i = " << i;
        }
    }
    EXPECT_EQ(world.region(1).node_count(), 2u);
    EXPECT_EQ(world.compose_count(), 2u) << "the rename must have rebuilt region 1 once";
}
