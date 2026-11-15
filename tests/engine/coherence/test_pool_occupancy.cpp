/**************************************************************************/
/* test_pool_occupancy.cpp                                                */
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
 * @file test_pool_occupancy.cpp
 * @brief Tests that PoolOccupancy counts each buffer once per pool and starts with the stated homes.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/pool_occupancy.hpp>

#include "partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PoolOccupancy, ABufferTwoNodesTouchIsCountedOncePerPool)
    {
        PartitionProblem problem;
        const std::size_t shared = add_buffer(problem, 100);
        PartitionNode writer;
        writer.writes.push_back(shared);
        PartitionNode reader;
        reader.reads.push_back(shared);
        reader.writes.push_back(add_buffer(problem, 50));
        problem.nodes = {writer, reader};

        PoolOccupancy occupancy(problem, 2, {std::nullopt, std::nullopt});
        occupancy.add(0, 0);
        EXPECT_EQ(occupancy.added_bytes(1, 0), 50U);
        EXPECT_EQ(occupancy.added_bytes(1, 1), 150U);
        occupancy.add(1, 0);
        EXPECT_EQ(occupancy.used(0), 150U);
        EXPECT_EQ(occupancy.used(1), 0U);
    }

    TEST(Small_PoolOccupancy, ABufferWithAStatedHomeIsHeldInItsPoolFromTheStart)
    {
        PartitionProblem problem;
        const std::size_t stated = add_buffer(problem, 70);
        PartitionNode reader;
        reader.reads.push_back(stated);
        problem.nodes = {reader};

        PoolOccupancy occupancy(problem, 2, {std::size_t{1}});
        EXPECT_EQ(occupancy.used(1), 70U);
        EXPECT_EQ(occupancy.added_bytes(0, 1), 0U);
        EXPECT_EQ(occupancy.added_bytes(0, 0), 70U);
    }
} // namespace
