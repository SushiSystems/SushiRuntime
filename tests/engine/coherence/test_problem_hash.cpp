/**************************************************************************/
/* test_problem_hash.cpp                                                  */
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
 * @file test_problem_hash.cpp
 * @brief Tests that problem_hash is equal for equal problems and differs when any part differs.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/problem_hash.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    PartitionProblem sample()
    {
        PartitionProblem problem;
        PartitionDevice device;
        device.key.ordinal = 1;
        device.copy_path = "peer";
        problem.devices = {device};
        PartitionBuffer buffer;
        buffer.handle = GlobalHandle::from_words(1, 1);
        buffer.bytes = 64;
        problem.buffers = {buffer};
        PartitionNode writer;
        writer.cost.operations = 10;
        writer.writes = {0};
        PartitionNode reader;
        reader.reads = {0};
        reader.split = PartitionSplit{0, 8};
        problem.nodes = {writer, reader};
        problem.edges = {PartitionEdge{0, 1, 64}};
        return problem;
    }

    TEST(Small_ProblemHash, EqualProblemsHashEqualAndEachChangedPartChangesTheHash)
    {
        const std::uint64_t base = problem_hash(sample());
        EXPECT_EQ(base, problem_hash(sample()));

        PartitionProblem cost = sample();
        cost.nodes[0].cost.operations = 11;
        EXPECT_NE(base, problem_hash(cost));

        PartitionProblem placed = sample();
        placed.nodes[1].placed = placed.devices[0].key;
        EXPECT_NE(base, problem_hash(placed));

        PartitionProblem split = sample();
        split.nodes[1].split.reset();
        EXPECT_NE(base, problem_hash(split));

        PartitionProblem bytes = sample();
        bytes.buffers[0].bytes = 128;
        EXPECT_NE(base, problem_hash(bytes));

        PartitionProblem edge = sample();
        edge.edges[0].bytes = 32;
        EXPECT_NE(base, problem_hash(edge));

        PartitionProblem path = sample();
        path.devices[0].copy_path = "host";
        EXPECT_NE(base, problem_hash(path));
    }
} // namespace
