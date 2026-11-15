/**************************************************************************/
/* test_planned_homes.cpp                                                 */
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
 * @file test_planned_homes.cpp
 * @brief Tests that planned_homes() homes each planned buffer on its first writer's device.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <SushiRuntime/coherence/partition_model.hpp>
#include <SushiRuntime/coherence/planned_homes.hpp>

#include "partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PlannedHomes, EachBufferOfAChainIsHomedOnTheDeviceOfTheNodeThatWritesIt)
    {
        const PartitionProblem problem = chain(2, 100, 2);
        const std::shared_ptr<const CostModel> costs = speed_model({1.0, 1.0});
        const Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        // Node 0 writes buffer 0 on device 1; node 1 reads buffer 0 and writes buffer 1 on device 0.
        const std::map<GlobalHandle, DeviceKey> homes = planned_homes(model.value(), std::vector<std::size_t>{1, 0});

        ASSERT_EQ(homes.size(), 2u);
        EXPECT_EQ(homes.at(problem.buffers[0].handle), host_key(2));
        EXPECT_EQ(homes.at(problem.buffers[1].handle), host_key(1));
    }

    TEST(Small_PlannedHomes, ABufferWithAStatedDeviceHasNoHome)
    {
        PartitionProblem problem = chain(1, 100, 2);
        problem.buffers[0].device = host_key(1);
        const std::shared_ptr<const CostModel> costs = speed_model({1.0, 1.0});
        const Result<PartitionModel, PartitionRefusal> model = PartitionModel::build(problem, *costs);
        ASSERT_TRUE(model.has_value());

        EXPECT_TRUE(planned_homes(model.value(), std::vector<std::size_t>{0}).empty());
    }
} // namespace
