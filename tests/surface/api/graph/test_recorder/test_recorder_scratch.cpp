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
 * @file test_recorder_scratch.cpp
 * @brief Tests the USM a Recorder hands out to a rule: its lifetime, replay and regions.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "recorder_fixtures.hpp"

using namespace SushiRuntime;

/** @brief Tests the USM a Recorder hands out to a rule: its lifetime, replay and regions. */
namespace RecorderScratchTests
{
    namespace
    {
        constexpr std::size_t K_COUNT = 1000;

        /** @brief Returns what the staged rule writes for one input value. */
        std::uint32_t staged(std::uint32_t value)
        {
            return (value + 1u) * 2u;
        }

        /**
         * @brief Records one rule of two nodes that hand their values through recorder scratch.
         *
         * The first node writes in[i] + 1 into scratch and the second writes twice that into out.
         * @param in  Holds at least K_COUNT values; must outlive the recorder.
         * @param out Holds at least K_COUNT values; must outlive the recorder.
         */
        void record_staged_rule(Recorder& recorder, const Buffer<std::uint32_t>& in,
                                const Buffer<std::uint32_t>& out)
        {
            std::uint32_t* stage =
                recorder.scratch<std::uint32_t>(K_COUNT, in.device_index(), in.residency()).value();
            const BufferRegion stage_region{stage, 0, K_COUNT * sizeof(std::uint32_t)};
            const std::uint32_t* source = in.data();
            std::uint32_t* target = out.data();

            auto rule = recorder.rule();
            recorder.kernel(K_COUNT, Access(Reads(in), Writes(stage_region)),
                            [source, stage](std::size_t i) { stage[i] = source[i] + 1u; });
            recorder.kernel(K_COUNT, Access(Reads(stage_region), Writes(out)),
                            [stage, target](std::size_t i) { target[i] = stage[i] * 2u; });
        }

        /** @brief Fills a buffer with a pattern that differs per seed. */
        void fill(Buffer<std::uint32_t>& buffer, std::uint32_t seed)
        {
            for (std::size_t i = 0; i < K_COUNT; ++i)
                buffer[i] = static_cast<std::uint32_t>(i) * 3u + seed;
        }
    } // namespace

    /** @brief Runs the RecorderScratch tests on a host runtime. */
    class Small_RecorderScratch : public RecorderHostTest
    {
    };

    /** @brief Tests that a consumer of a rule's output sees what passed through its scratch. */
    TEST_F(Small_RecorderScratch, AConsumerSeesWhatPassedThroughTheScratch)
    {
        const Runtime& rt = runtime();
        auto in   = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto copy = rt.buffer<std::uint32_t>(K_COUNT).value();
        fill(in, 5);
        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            out[i] = 0;
            copy[i] = 0;
        }
        const std::uint32_t* o = out.data();
        std::uint32_t* c = copy.data();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);
        g.add(K_COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i] + 1u; });
        g.run().value();

        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            ASSERT_EQ(out[i], staged(in[i])) << "i = " << i;
            ASSERT_EQ(copy[i], staged(in[i]) + 1u) << "i = " << i;
        }
    }

    /** @brief Tests that each run of a replayed rule reads its input as it stands. */
    TEST_F(Small_RecorderScratch, EachRunReadsItsInputAsItStands)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);

        for (std::uint32_t step = 0; step < 4; ++step)
        {
            fill(in, step * 11u);
            g.run().value();
            for (std::size_t i = 0; i < K_COUNT; ++i)
                ASSERT_EQ(out[i], staged(in[i])) << "step " << step << ", i = " << i;
        }
    }

    /** @brief Tests that a replayed rule compiles once. */
    TEST_F(Small_RecorderScratch, AReplayedRuleCompilesOnce)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);

        for (std::uint32_t step = 0; step < 4; ++step)
            g.run().value();

        EXPECT_EQ(g.compile_count(), 1u);
    }

    TEST_F(Small_RecorderScratch, ScratchSurvivesARecompile)
    {
        const Runtime& rt = runtime();
        auto in   = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto copy = rt.buffer<std::uint32_t>(K_COUNT).value();
        fill(in, 1);
        const std::uint32_t* o = out.data();
        std::uint32_t* c = copy.data();

        auto g = rt.graph();
        record_staged_rule(g.recorder(), in, out);
        g.run().value();
        for (std::size_t i = 0; i < K_COUNT; ++i)
            ASSERT_EQ(out[i], staged(in[i])) << "first plan, i = " << i;

        // A node recorded after a run makes the next run build a second plan.
        g.add(K_COUNT, Reads(out), Writes(copy), [o, c](std::size_t i) { c[i] = o[i]; });
        fill(in, 9);
        g.run().value();

        for (std::size_t i = 0; i < K_COUNT; ++i)
        {
            ASSERT_EQ(out[i], staged(in[i])) << "second plan, i = " << i;
            ASSERT_EQ(copy[i], staged(in[i])) << "second plan, i = " << i;
        }
    }


    /** @brief Tests that regions record rules with scratch and pass values through it across rounds. */
    TEST_F(Small_RecorderScratch, RegionsRecordRulesWithScratch)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto mid = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto world = rt.dynamic_graph();
        record_staged_rule(world.region(1).recorder(), in, mid);
        record_staged_rule(world.region(2).recorder(), mid, out);

        for (std::uint32_t round = 0; round < 3; ++round)
        {
            fill(in, round * 7u);
            // Renaming a rule changes the recording, so the last round replays region 1.
            if (round == 2)
                world.region(1).name("staged");
            world.run().value();
            for (std::size_t i = 0; i < K_COUNT; ++i)
            {
                ASSERT_EQ(mid[i], staged(in[i])) << "round " << round << ", i = " << i;
                ASSERT_EQ(out[i], staged(staged(in[i]))) << "round " << round << ", i = " << i;
            }
        }
    }

    /** @brief Tests that renaming a region's rule rebuilds that region once. */
    TEST_F(Small_RecorderScratch, RenamingARegionsRuleRebuildsThatRegionOnce)
    {
        const Runtime& rt = runtime();
        auto in  = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto mid = rt.buffer<std::uint32_t>(K_COUNT).value();
        auto out = rt.buffer<std::uint32_t>(K_COUNT).value();

        auto world = rt.dynamic_graph();
        record_staged_rule(world.region(1).recorder(), in, mid);
        record_staged_rule(world.region(2).recorder(), mid, out);
        world.run().value();
        ASSERT_EQ(world.compose_count(), 1u);

        world.region(1).name("staged");
        world.run().value();

        EXPECT_EQ(world.region(1).node_count(), 2u);
        EXPECT_EQ(world.compose_count(), 2u) << "the rename must have rebuilt region 1 once";
    }

    /** @brief Tests that scratch no device can hold comes back as AllocationFailed. */
    TEST_F(Small_RecorderScratch, AScratchRequestNoDeviceCanSatisfyReturnsAllocationFailed)
    {
        auto graph = runtime().graph();
        // Half the address space in bytes: the count fits std::size_t and no device has the room.
        const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

        const auto refused =
            graph.recorder().scratch<std::uint8_t>(count, DeviceIndex{0}, Residency::Shared);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().code, ErrorCode::AllocationFailed);
    }

    /** @brief Tests that a small scratch request is granted on a recorder that refused a large one. */
    TEST_F(Small_RecorderScratch, ASmallScratchRequestIsGrantedAfterALargeOneFailed)
    {
        auto graph = runtime().graph();
        const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;
        (void)graph.recorder().scratch<std::uint8_t>(count, DeviceIndex{0}, Residency::Shared);

        const auto granted =
            graph.recorder().scratch<std::uint8_t>(16, DeviceIndex{0}, Residency::Shared);
        EXPECT_TRUE(granted.has_value());
    }
} // namespace RecorderScratchTests
