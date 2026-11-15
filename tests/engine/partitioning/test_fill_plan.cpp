/**************************************************************************/
/* test_fill_plan.cpp                                                     */
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

/**
 * @file test_fill_plan.cpp
 * @brief Tests that a fill plan walks the devices fastest first and never returns, and that the floor fits.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/fill_plan.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_FillPlan, AChainLargerThanTheFastestPoolCrossesEachDeviceBoundaryOnce)
    {
        const PartitionProblem problem = state_chain(7, 300, 3);
        const auto costs = speed_model({1.0, 4.0, 2.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        const FillWalk walk = fill_plan(model.value(), {0, 1, 2});

        ASSERT_TRUE(walk.fits);
        const std::vector<std::size_t> expected{1, 1, 1, 2, 2, 0, 0};
        EXPECT_EQ(walk.placement, expected);
        std::size_t crossings = 0;
        for (std::size_t node = 1; node < walk.placement.size(); ++node)
        {
            if (walk.placement[node] != walk.placement[node - 1])
                ++crossings;
        }
        EXPECT_EQ(crossings, 2U);
    }

    TEST(Small_FillPlan, TheLastDeviceTakesWhatIsLeftAndTheWalkReportsItDoesNotFit)
    {
        const PartitionProblem problem = state_chain(4, 400, 1);
        const auto costs = speed_model({1.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        const FillWalk walk = fill_plan(model.value(), {0});

        EXPECT_FALSE(walk.fits);
        EXPECT_EQ(walk.placement, (std::vector<std::size_t>{0, 0, 0, 0}));
    }

    TEST(Small_FillPlan, TheFloorIsAFittingPlanAndNoneExistsWhenNothingFits)
    {
        const auto costs = speed_model({1.0, 4.0, 2.0});
        const PartitionProblem fitting = chain(7, 300, 3);
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(fitting, *costs);
        ASSERT_TRUE(model.has_value());
        const std::optional<FloorPlan> floor = floor_plan(model.value());
        ASSERT_TRUE(floor.has_value());
        EXPECT_TRUE(model.value().fits(floor->placement));
        EXPECT_DOUBLE_EQ(floor->step_time, model.value().step_time(floor->placement));

        const PartitionProblem too_large = state_chain(20, 300, 3);
        Result<PartitionModel, PartitionRefusal> large = PartitionModel::build(too_large, *costs);
        ASSERT_TRUE(large.has_value());
        EXPECT_FALSE(floor_plan(large.value()).has_value());
    }

    TEST(Small_FillPlan, TheFloorWalksQuadraticallyManyCandidates)
    {
        EXPECT_EQ(floor_candidate_count(1), 1U);
        EXPECT_EQ(floor_candidate_count(3), 6U);
        EXPECT_EQ(floor_candidate_count(16), 136U);
    }

    TEST(Small_FillPlan, AnAddedDeviceNeverRaisesTheFloorAboveAPrefixOfTheOldDevices)
    {
        const PartitionProblem larger = chain(7, 300, 3);
        const auto costs = speed_model({1.0, 4.0, 2.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(larger, *costs);
        ASSERT_TRUE(model.has_value());

        const std::optional<FloorPlan> floor = floor_plan(model.value());

        ASSERT_TRUE(floor.has_value());
        // Devices 0 and 1 are the old machine; by speed its prefixes are {1} and {0, 1}.
        for (const std::vector<std::size_t>& prefix : {std::vector<std::size_t>{1}, std::vector<std::size_t>{0, 1}})
        {
            const FillWalk walk = fill_plan(model.value(), prefix);
            if (walk.fits)
                EXPECT_LE(floor->step_time, model.value().step_time(walk.placement));
        }
    }
} // namespace
