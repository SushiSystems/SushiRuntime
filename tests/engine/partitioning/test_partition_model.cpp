/**************************************************************************/
/* test_partition_model.cpp                                               */
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
 * @file test_partition_model.cpp
 * @brief Tests that PartitionModel counts a shared pool once, prices a step and refuses malformed problems.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/partition_model.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PartitionModel, DevicesThatShareAPoolCountItOnce)
    {
        PartitionProblem problem = chain(2, 100, 2);
        for (PartitionDevice& device : problem.devices)
            device.pool = 7;
        const auto costs = speed_model({1.0, 2.0});

        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);

        ASSERT_TRUE(model.has_value());
        EXPECT_EQ(model.value().pool_count(), 1U);
        EXPECT_EQ(model.value().usable_bytes(0), 900U);
        EXPECT_DOUBLE_EQ(model.value().edge_time(0, 0, 1), 0.0);
    }

    TEST(Small_PartitionModel, AStepAddsTheCopyOfAnEdgeThatCrossesTwoPools)
    {
        const PartitionProblem problem = chain(2, 100, 2);
        const auto costs = speed_model({1.0, 1.0});
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        const double node = 1000.0 / 2000.0;
        const double copy = 0.25 + 100.0 / 500.0;
        EXPECT_DOUBLE_EQ(model.value().step_time({0, 0}), 2.0 * node);
        EXPECT_DOUBLE_EQ(model.value().step_time({0, 1}), 2.0 * node + copy);
    }

    TEST(Small_PartitionModel, AnEdgeThatRunsBackwardIsRefused)
    {
        PartitionProblem problem = chain(2, 100, 1);
        problem.edges.push_back(PartitionEdge{1, 0, 0});
        const auto costs = speed_model({1.0});

        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);

        ASSERT_FALSE(model.has_value());
        EXPECT_EQ(model.error().error.code, ErrorCode::InvalidArgument);
    }

    TEST(Small_PartitionModel, ANodeThatWritesABufferWithAStatedDeviceIsFixedToIt)
    {
        PartitionProblem problem = chain(1, 100, 2);
        problem.buffers[0].device = host_key(2);
        const auto costs = speed_model({1.0, 1.0});

        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);

        ASSERT_TRUE(model.has_value());
        ASSERT_TRUE(model.value().fixed_device(0).has_value());
        EXPECT_EQ(*model.value().fixed_device(0), 1U);
    }

    TEST(Small_PartitionModel, ANodePlacedOnADeviceWithNoProfileIsRefused)
    {
        PartitionProblem problem = chain(1, 100, 2);
        problem.nodes[0].placed = host_key(2);
        const auto costs = speed_model({1.0});

        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);

        ASSERT_FALSE(model.has_value());
        EXPECT_EQ(model.error().error.code, ErrorCode::InvalidState);
    }
} // namespace
