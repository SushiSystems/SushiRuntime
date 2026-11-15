/**************************************************************************/
/* test_copy_plan_replay.cpp                                              */
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
 * @file test_copy_plan_replay.cpp
 * @brief Tests that a static graph plans its copies at compile and replays them without the planner.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests the copy plan a static graph keeps with its compiled plan. */
namespace StaticCopyPlanReplayTests
{
    namespace
    {
        /** @brief The number of elements in each buffer. */
        constexpr std::size_t K_COUNT = 4096;

        /** @brief The multiplier the writing node uses; odd, so the elements take different values. */
        constexpr std::uint32_t K_SPREAD = 2654435761u;

        /** @brief Returns the host reference of the buffer the reading node writes. */
        std::vector<std::uint32_t> reference_result()
        {
            std::vector<std::uint32_t> expected(K_COUNT);
            for (std::size_t index = 0; index < K_COUNT; ++index)
                expected[index] = static_cast<std::uint32_t>(index) * K_SPREAD + 1u;
            return expected;
        }
    } // namespace

    /** @brief Fixture of the static replay tests; they skip on a runtime with fewer than two devices. */
    class Medium_StaticCopyPlanReplay : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Skips the test when the runtime has no second device to copy to. */
            void SetUp() override
            {
                if (runtime().advanced().devices().size() < 2)
                    GTEST_SKIP() << "the runtime drives one device; a copy plan needs two.";
            }

            /**
             * @brief Records a node that writes a buffer on device 0 and a node that reads it on
             *        device 1, so every step needs one planned copy.
             */
            void record(Graph& graph, Buffer<std::uint32_t>& written, Buffer<std::uint32_t>& result)
            {
                Recorder& recorder = graph.recorder();
                recorder.captured_kernel(K_COUNT, Access(Reads(), Writes(written)), std::make_tuple(written.data()),
                                         std::vector<GlobalHandle>{written.handle()},
                                         [](std::size_t index, std::uint32_t* target)
                                         { target[index] = static_cast<std::uint32_t>(index) * K_SPREAD; });
                recorder.captured_kernel(K_COUNT, Access(Reads(written), Writes(result)),
                                         std::make_tuple(static_cast<const std::uint32_t*>(written.data()),
                                                         result.data()),
                                         std::vector<GlobalHandle>{written.handle(), result.handle()},
                                         [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                                         { target[index] = source[index] + 1u; });
            }
    };

    /** @brief Tests that the first compile of a graph with nodes on two devices plans its copies. */
    TEST_F(Medium_StaticCopyPlanReplay, CopiesArePlannedAtCompile)
    {
        const Runtime& rt = runtime();
        auto written = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{0}).value();
        auto result = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{1}).value();
        auto graph = rt.graph();
        record(graph, written, result);

        ASSERT_EQ(graph.coherence_plan_count(), 0u);
        ASSERT_TRUE(graph.run());
        EXPECT_EQ(graph.compile_count(), 1u);
        EXPECT_EQ(graph.coherence_plan_count(), 1u);
    }

    /** @brief Tests that a replayed step runs no planner code and compiles nothing, by the counters. */
    TEST_F(Medium_StaticCopyPlanReplay, AReplayedStepRunsNoPlannerCode)
    {
        const Runtime& rt = runtime();
        auto written = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{0}).value();
        auto result = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{1}).value();
        auto graph = rt.graph();
        record(graph, written, result);

        // The first plan leaves the copy resident and is not steady; the second plans the steady state.
        ASSERT_TRUE(graph.run());
        ASSERT_TRUE(graph.run());
        const std::size_t planned = graph.coherence_plan_count();
        const std::size_t compiled = graph.compile_count();
        for (int step = 0; step < 4; ++step)
            ASSERT_TRUE(graph.run());

        EXPECT_EQ(graph.coherence_plan_count(), planned);
        EXPECT_EQ(graph.compile_count(), compiled);
    }

    /** @brief Tests that a replayed step leaves the bytes of the host reference. */
    TEST_F(Medium_StaticCopyPlanReplay, AReplayedStepLeavesTheBytesOfTheHostReference)
    {
        const Runtime& rt = runtime();
        auto written = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{0}).value();
        auto result = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{1}).value();
        auto graph = rt.graph();
        record(graph, written, result);

        ASSERT_TRUE(graph.run());
        const std::vector<std::uint32_t> fresh = TestSupport::contents(result);
        ASSERT_TRUE(graph.run());
        const std::size_t planned = graph.coherence_plan_count();
        ASSERT_TRUE(graph.run());
        ASSERT_EQ(graph.coherence_plan_count(), planned);
        const std::vector<std::uint32_t> replayed = TestSupport::contents(result);

        EXPECT_EQ(fresh, reference_result());
        EXPECT_EQ(replayed, reference_result());
    }
} // namespace StaticCopyPlanReplayTests
