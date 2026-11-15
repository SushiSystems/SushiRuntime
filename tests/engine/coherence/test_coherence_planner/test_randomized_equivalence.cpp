/**************************************************************************/
/* test_randomized_equivalence.cpp                                        */
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
 * @file test_randomized_equivalence.cpp
 * @brief Tests that the planner and the planned nodes give the bytes of a sequential reference.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "graph_generator.hpp"
#include "planned_interpreter.hpp"
#include "reference_interpreter.hpp"

using namespace SushiRuntime::Internal::Coherence;
using namespace SushiRuntime::Internal::Coherence::Equivalence;

namespace
{
    constexpr std::size_t K_SEED_COUNT = 32;
    constexpr std::uint64_t K_FIRST_SEED = 0x5EED0000ULL;

    /** @brief Returns the fixed seeds the test runs, so a failure names a seed that always reproduces. */
    std::vector<std::uint64_t> fixed_seeds()
    {
        std::vector<std::uint64_t> seeds;
        for (std::size_t index = 0; index < K_SEED_COUNT; ++index)
            seeds.push_back(K_FIRST_SEED + index);
        return seeds;
    }

    /** @brief Runs one seeded graph on the planner and on the reference. */
    class Medium_CoherenceEquivalence : public ::testing::TestWithParam<std::uint64_t>
    {
        protected:
            /** @brief Returns a queue for the planned nodes to take, or nothing when SYCL offers none. */
            static std::optional<sycl::queue> any_queue()
            {
                try
                {
                    return sycl::queue{};
                }
                catch (const sycl::exception&)
                {
                    return std::nullopt;
                }
            }
    };

    TEST_P(Medium_CoherenceEquivalence, EverySeededGraphGivesEqualBytesOnThePlannerAndTheReference)
    {
        std::optional<sycl::queue> queue = any_queue();
        if (!queue)
            GTEST_SKIP() << "SYCL offers no queue for host_work to take.";
        const std::uint64_t seed = GetParam();
        SCOPED_TRACE(::testing::Message() << "failing seed: " << seed);
        const GraphCase graph = generate_graph(seed);

        PlannedRun planned;
        try
        {
            planned = run_planned(graph, *queue);
        }
        catch (const std::exception& failure)
        {
            FAIL() << "seed " << seed << ": " << failure.what();
        }
        const Outcome reference = run_reference(graph);

        EXPECT_EQ(planned.outcome.digests, reference.digests) << "a node read bytes the reference did not";
        EXPECT_EQ(planned.outcome.buffers, reference.buffers) << "the bytes at the end differ";
        EXPECT_EQ(planned.leftover, 0U) << "the plan left an instance behind";
    }

    TEST_P(Medium_CoherenceEquivalence, TwoRunsOfOneSeedGiveTheSamePlanAndTheSameBytes)
    {
        std::optional<sycl::queue> queue = any_queue();
        if (!queue)
            GTEST_SKIP() << "SYCL offers no queue for host_work to take.";
        SCOPED_TRACE(::testing::Message() << "failing seed: " << GetParam());
        const GraphCase graph = generate_graph(GetParam());

        PlannedRun first;
        PlannedRun again;
        try
        {
            first = run_planned(graph, *queue);
            again = run_planned(graph, *queue);
        }
        catch (const std::exception& failure)
        {
            FAIL() << "seed " << GetParam() << ": " << failure.what();
        }

        EXPECT_EQ(first.steps, again.steps);
        EXPECT_EQ(first.outcome, again.outcome);
    }

    INSTANTIATE_TEST_SUITE_P(Seeds, Medium_CoherenceEquivalence, ::testing::ValuesIn(fixed_seeds()));
} // namespace
