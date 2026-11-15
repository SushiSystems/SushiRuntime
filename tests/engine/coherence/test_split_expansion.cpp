/**************************************************************************/
/* test_split_expansion.cpp                                               */
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
 * @file test_split_expansion.cpp
 * @brief Tests expand_splits() and planned_splits(): one node per part, disjoint written ranges.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/split_expansion.hpp>

#include "partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns a chain of three nodes whose middle node is cut into a 7 and a 3 part. */
    struct CutChain
    {
        PartitionProblem problem = chain(3, 1000, 2);
        std::vector<std::vector<SizedPart>> parts{3};
        std::vector<DeviceKey> devices{host_key(1), host_key(2)};

        CutChain()
        {
            problem.nodes[1].split = PartitionSplit{0, 10};
            parts[1] = {SizedPart{0, SplitPart{0, 0, 7, 0, 2}}, SizedPart{1, SplitPart{0, 7, 10, 1, 2}}};
        }
    };

    /** @brief Tests that a cut node stands as one placed node per part with its share of the cost and the written bytes. */
    TEST(Small_SplitExpansion, ACutNodeStandsAsOnePlacedNodePerPart)
    {
        const CutChain cut;

        const SplitExpansion expansion = expand_splits(cut.problem, cut.parts, cut.devices);

        ASSERT_EQ(expansion.problem.nodes.size(), 4u);
        EXPECT_EQ(expansion.origin, (std::vector<std::size_t>{0, 1, 1, 2}));
        EXPECT_EQ(expansion.problem.nodes[1].placed, host_key(1));
        EXPECT_EQ(expansion.problem.nodes[2].placed, host_key(2));
        EXPECT_EQ(expansion.problem.nodes[1].cost.operations, 700u);
        EXPECT_EQ(expansion.problem.nodes[2].cost.operations, 300u);
        EXPECT_EQ(expansion.problem.nodes[1].reads, cut.problem.nodes[1].reads);
        EXPECT_EQ(expansion.problem.nodes[2].reads, cut.problem.nodes[1].reads);
        EXPECT_EQ(expansion.problem.buffers[expansion.problem.nodes[1].writes[0]].bytes, 700u);
        EXPECT_EQ(expansion.problem.buffers[expansion.problem.nodes[2].writes[0]].bytes, 300u);
    }

    /** @brief Tests that an edge into a cut node reaches every part whole and an edge out of it carries each part's share. */
    TEST(Small_SplitExpansion, EdgesFollowTheParts)
    {
        const CutChain cut;

        const SplitExpansion expansion = expand_splits(cut.problem, cut.parts, cut.devices);

        ASSERT_EQ(expansion.problem.edges.size(), 4u);
        const std::vector<PartitionEdge>& edges = expansion.problem.edges;
        EXPECT_EQ(edges[0].from, 0u);
        EXPECT_EQ(edges[0].to, 1u);
        EXPECT_EQ(edges[0].bytes, 1000u);
        EXPECT_EQ(edges[1].to, 2u);
        EXPECT_EQ(edges[1].bytes, 1000u);
        EXPECT_EQ(edges[2].from, 1u);
        EXPECT_EQ(edges[2].to, 3u);
        EXPECT_EQ(edges[2].bytes, 700u);
        EXPECT_EQ(edges[3].from, 2u);
        EXPECT_EQ(edges[3].bytes, 300u);
        for (const PartitionEdge& edge : edges)
            EXPECT_LT(edge.from, edge.to);
    }

    /** @brief Tests that the planned parts write disjoint ranges that cover the buffer and read their inputs whole. */
    TEST(Small_SplitExpansion, PlannedPartsWriteDisjointRangesAndReadWhole)
    {
        const CutChain cut;

        const std::vector<PlannedSplit> splits = planned_splits(cut.problem, cut.parts, cut.devices);

        ASSERT_EQ(splits.size(), 1u);
        EXPECT_EQ(splits[0].node, 1u);
        ASSERT_EQ(splits[0].parts.size(), 2u);
        const PlannedSplitPart& first = splits[0].parts[0];
        const PlannedSplitPart& second = splits[0].parts[1];
        EXPECT_EQ(first.device, host_key(1));
        EXPECT_EQ(second.device, host_key(2));
        ASSERT_EQ(first.writes.size(), 1u);
        EXPECT_EQ(first.writes[0].begin, 0u);
        EXPECT_EQ(first.writes[0].end, 700u);
        EXPECT_EQ(second.writes[0].begin, 700u);
        EXPECT_EQ(second.writes[0].end, 1000u);
        ASSERT_EQ(first.reads.size(), 1u);
        EXPECT_EQ(first.reads[0].begin, 0u);
        EXPECT_EQ(first.reads[0].end, 1000u);
        EXPECT_EQ(second.reads[0].handle, first.reads[0].handle);
        EXPECT_EQ(second.reads[0].end, 1000u);
    }

    /** @brief Tests that a node with fewer than two parts stays whole. */
    TEST(Small_SplitExpansion, ANodeWithOnePartStaysWhole)
    {
        CutChain cut;
        cut.parts[1].resize(1);

        const SplitExpansion expansion = expand_splits(cut.problem, cut.parts, cut.devices);

        EXPECT_EQ(expansion.problem.nodes.size(), 3u);
        EXPECT_TRUE(planned_splits(cut.problem, cut.parts, cut.devices).empty());
    }
} // namespace
