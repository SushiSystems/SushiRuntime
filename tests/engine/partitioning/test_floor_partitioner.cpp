/**************************************************************************/
/* test_floor_partitioner.cpp                                             */
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
 * @file test_floor_partitioner.cpp
 * @brief Tests that FloorPartitioner answers with the floor plan and refuses a job no fill plan fits.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/floor_partitioner.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_FloorPartitioner, TwoIndependentNodesGoToOneDeviceAtTheFloorsStepTime)
    {
        const std::shared_ptr<const CostModel> costs = speed_model({1.0, 1.0});
        const FloorPartitioner floor(costs);
        const ListSchedulingPartitioner scheduler(costs);

        const Result<PartitionAssignment, PartitionRefusal> answer = floor.partition(independent_nodes(2, 2));

        ASSERT_TRUE(answer.has_value());
        EXPECT_TRUE(answer.value().floor_taken);
        ASSERT_EQ(answer.value().node_devices.size(), 2u);
        EXPECT_EQ(answer.value().node_devices[0], answer.value().node_devices[1]);
        // Each node takes 1000 / 2000 seconds on either device, so one device alone runs both in one second.
        EXPECT_EQ(answer.value().step_time, 1.0);
        EXPECT_LT(scheduler.partition(independent_nodes(2, 2)).value().step_time, answer.value().step_time);
    }

    TEST(Small_FloorPartitioner, AJobNoFillPlanFitsIsRefusedWithAllocationFailed)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.writes.push_back(add_buffer(problem, 2000));
        problem.nodes.push_back(node);

        const Result<PartitionAssignment, PartitionRefusal> answer =
            FloorPartitioner(speed_model({1.0, 1.0})).partition(problem);

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().to_error().code, ErrorCode::AllocationFailed);
    }
} // namespace
