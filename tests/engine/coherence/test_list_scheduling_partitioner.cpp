/**************************************************************************/
/* test_list_scheduling_partitioner.cpp                                   */
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
 * @file test_list_scheduling_partitioner.cpp
 * @brief Tests that ListSchedulingPartitioner refuses with the shortfall per pool, is deterministic and homes planned buffers.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/assignment_hash.hpp>
#include <SushiRuntime/coherence/fill_plan.hpp>
#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>

#include "partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_ListSchedulingPartitioner, ANodeLargerThanEveryPoolIsRefusedWithTheShortfallOfEachPool)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.writes.push_back(add_buffer(problem, 2000));
        problem.nodes.push_back(node);

        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().error.code, ErrorCode::AllocationFailed);
        ASSERT_EQ(result.error().pools.size(), 2U);
        for (std::size_t pool = 0; pool < 2; ++pool)
        {
            EXPECT_EQ(result.error().pools[pool].pool, pool + 1);
            EXPECT_EQ(result.error().pools[pool].required, 2000U);
            EXPECT_EQ(result.error().pools[pool].usable, 900U);
            EXPECT_EQ(result.error().pools[pool].missing(), 1100U);
        }
    }

    TEST(Small_ListSchedulingPartitioner, AJobLargerThanThePoolsTogetherIsRefusedWithTheShortfallOfEachPool)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        for (int index = 0; index < 3; ++index)
        {
            PartitionNode node;
            node.cost.operations = 1000;
            node.writes.push_back(add_buffer(problem, 600));
            problem.nodes.push_back(node);
        }

        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().error.code, ErrorCode::AllocationFailed);
        ASSERT_EQ(result.error().pools.size(), 2U);
        EXPECT_EQ(result.error().pools[0].required, 600U);
        EXPECT_EQ(result.error().pools[0].missing(), 0U);
        EXPECT_EQ(result.error().pools[1].required, 1200U);
        EXPECT_EQ(result.error().pools[1].missing(), 300U);
    }

    TEST(Small_ListSchedulingPartitioner, AssignmentsHashEqualAcrossRunsWithOneProfileFile)
    {
        const PartitionProblem first_problem = chain(7, 300, 3);
        const PartitionProblem second_problem = chain(7, 300, 3);
        const ListSchedulingPartitioner first(speed_model({1.0, 4.0, 2.0}));
        const ListSchedulingPartitioner second(speed_model({1.0, 4.0, 2.0}));

        const Result<PartitionAssignment, PartitionRefusal> one = first.partition(first_problem);
        const Result<PartitionAssignment, PartitionRefusal> two = second.partition(second_problem);
        const Result<PartitionAssignment, PartitionRefusal> again = first.partition(first_problem);

        ASSERT_TRUE(one.has_value());
        ASSERT_TRUE(two.has_value());
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(assignment_hash(one.value()), assignment_hash(two.value()));
        EXPECT_EQ(assignment_hash(one.value()), assignment_hash(again.value()));
        EXPECT_EQ(one.value().node_devices, two.value().node_devices);
    }

    TEST(Small_ListSchedulingPartitioner, APlannedBufferIsHomedOnTheDeviceOfItsFirstWriter)
    {
        const PartitionProblem problem = chain(3, 100, 2);
        const ListSchedulingPartitioner partitioner(speed_model({1.0, 2.0}));

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_TRUE(result.has_value());
        for (std::size_t buffer = 0; buffer < problem.buffers.size(); ++buffer)
        {
            const std::optional<DeviceKey> home = result.value().home_of(problem.buffers[buffer].handle);
            ASSERT_TRUE(home.has_value());
            EXPECT_EQ(*home, result.value().node_devices[buffer]);
        }
    }

    TEST(Small_ListSchedulingPartitioner, APlanIsNeverModelledSlowerThanItsFloor)
    {
        const PartitionProblem problem = chain(7, 300, 3);
        const auto costs = speed_model({1.0, 4.0, 2.0});
        const ListSchedulingPartitioner partitioner(costs);

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);
        Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(model.has_value());
        const std::optional<FloorPlan> floor = floor_plan(model.value());
        ASSERT_TRUE(floor.has_value());
        EXPECT_LE(result.value().step_time, floor->step_time);
    }

    TEST(Small_ListSchedulingPartitioner, ADeviceWithNoProfileReceivesNoNode)
    {
        PartitionProblem problem = chain(4, 100, 2);
        problem.devices.push_back(PartitionDevice{host_key(9), 9, "pcie"});
        const ListSchedulingPartitioner partitioner(speed_model({1.0, 2.0}));

        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_TRUE(result.has_value());
        for (const DeviceKey& device : result.value().node_devices)
            EXPECT_NE(device, host_key(9));
    }
} // namespace
