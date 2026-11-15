/**************************************************************************/
/* test_partition_assignment.cpp                                          */
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
 * @file test_partition_assignment.cpp
 * @brief Tests that PartitionAssignment returns the home of a planned buffer and nothing for another.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/partition_assignment.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PartitionAssignment, HomeOfReturnsTheHomeOfAHomedBufferAndNothingOtherwise)
    {
        DeviceKey device;
        device.ordinal = 3;
        PartitionAssignment assignment;
        assignment.homes.emplace(GlobalHandle::from_words(1, 1), device);

        ASSERT_TRUE(assignment.home_of(GlobalHandle::from_words(1, 1)).has_value());
        EXPECT_EQ(*assignment.home_of(GlobalHandle::from_words(1, 1)), device);
        EXPECT_FALSE(assignment.home_of(GlobalHandle::from_words(1, 2)).has_value());
    }
} // namespace
