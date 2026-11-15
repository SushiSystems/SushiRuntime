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
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <SushiRuntime/partitioning/problem_hash.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    PartitionProblem sample()
    {
        PartitionProblem problem;
        PartitionDevice device;
        device.key.ordinal = 1;
        device.copy_path = TransferPathID::named("peer");
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
        path.devices[0].copy_path = TransferPathID::named("host");
        EXPECT_NE(base, problem_hash(path));

        PartitionProblem ranges = sample();
        ranges.nodes[1].read_ranges = std::vector<BufferRange>{BufferRange{ranges.buffers[0].handle, 0, 32}};
        EXPECT_NE(base, problem_hash(ranges));
    }

    /** @brief Returns a split of axis 0 over 8 whose part reads 8 bytes per index of buffer 0, from offset. */
    [[nodiscard]] PartitionSplit offset_split(std::size_t offset)
    {
        PartitionSplit split{0, 8};
        split.regions = [offset](const SplitPart& part)
        {
            SplitRegions regions;
            const auto begin = static_cast<std::size_t>(part.begin * 8);
            const auto length = static_cast<std::size_t>((part.end - part.begin) * 8);
            regions.reads.push_back(BufferRegion(GlobalHandle::from_words(1, 1), offset + begin, length));
            return regions;
        };
        return split;
    }

    TEST(Small_ProblemHash, ASplitWhoseRegionsChangeChangesTheHash)
    {
        PartitionProblem first = sample();
        first.devices.push_back(first.devices[0]);
        first.devices[1].key.ordinal = 2;
        first.nodes[1].split = offset_split(0);
        PartitionProblem same = first;
        same.nodes[1].split = offset_split(0);
        PartitionProblem moved = first;
        moved.nodes[1].split = offset_split(4);

        EXPECT_EQ(problem_hash(first), problem_hash(same));
        EXPECT_NE(problem_hash(first), problem_hash(moved));
    }
} // namespace
