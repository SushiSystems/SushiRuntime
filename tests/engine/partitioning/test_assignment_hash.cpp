/**************************************************************************/
/* test_assignment_hash.cpp                                               */
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
 * @file test_assignment_hash.cpp
 * @brief Tests that assignment_hash is equal for equal assignments and differs when a device differs.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/assignment_hash.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    PartitionAssignment sample(std::uint32_t ordinal)
    {
        DeviceKey device;
        device.ordinal = ordinal;
        PartitionAssignment assignment;
        assignment.node_devices = {device, device};
        assignment.homes.emplace(GlobalHandle::from_words(1, 1), device);
        assignment.step_time = 0.5;
        return assignment;
    }

    TEST(Small_AssignmentHash, EqualAssignmentsHashEqualAndAnotherDeviceChangesTheHash)
    {
        EXPECT_EQ(assignment_hash(sample(1)), assignment_hash(sample(1)));
        EXPECT_NE(assignment_hash(sample(1)), assignment_hash(sample(2)));

        PartitionAssignment floor = sample(1);
        floor.floor_taken = true;
        EXPECT_NE(assignment_hash(sample(1)), assignment_hash(floor));
    }
} // namespace
