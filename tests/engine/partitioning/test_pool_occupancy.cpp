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
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/partitioning/pool_occupancy.hpp>

#include "support/partition_problem_builder.hpp"

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

    TEST(Small_PoolOccupancy, AChainHoldsOnlyTheTwoBuffersLiveAtEachLink)
    {
        // Buffer i lives from node i to node i + 1; the last one leaves the step, so it lives to the end.
        const PartitionProblem problem = chain(4, 300, 1);
        PoolOccupancy occupancy(problem, 1, std::vector<std::optional<std::size_t>>(4));
        occupancy.add(2, 0);
        occupancy.add(0, 0);
        occupancy.add(3, 0);
        occupancy.add(1, 0);
        EXPECT_EQ(occupancy.used(0), 600U);
    }

    TEST(Small_PoolOccupancy, ABufferReadBeforeAnyWriteIsLiveForTheWholeStep)
    {
        // The input is carried in, so it is live through node 1 although only node 0 reads it.
        PartitionProblem problem;
        const std::size_t input = add_buffer(problem, 400);
        PartitionNode reader;
        reader.reads.push_back(input);
        PartitionNode other;
        other.writes.push_back(add_buffer(problem, 100));
        problem.nodes = {reader, other};

        PoolOccupancy occupancy(problem, 1, {std::nullopt, std::nullopt});
        occupancy.add(0, 0);
        EXPECT_EQ(occupancy.added_bytes(1, 0), 100U);
    }
} // namespace
