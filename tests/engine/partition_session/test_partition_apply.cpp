/**************************************************************************/
/* test_partition_apply.cpp                                               */
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
 * @file test_partition_apply.cpp
 * @brief Tests that apply_partition() puts movable nodes on their assigned devices, so planned buffers get their homes.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <set>
#include <vector>

#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/partition_session/partition_apply.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>
#include <SushiRuntime/task/placed_split.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Graph::InMemoryPlacementMemory;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    /** @brief Holds a chain of three nodes over planned buffers on two host devices of unequal speed. */
    class Small_PartitionApply : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                for (sushi_ptr<Node>& node : nodes_)
                {
                    node = make_sushi<Node>();
                    NodeCost cost;
                    cost.operations = 1000;
                    node->metadata.cost = cost;
                }
                for (GlobalHandle& handle : buffers_)
                {
                    handle = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
                    ASSERT_EQ(directory_.describe(handle, 300, ElementLayout{}, MemorySpaceID(0)), HandleStatus::Ok);
                }
                nodes_[0]->io_writes_.emplace_back(buffers_[0]);
                nodes_[1]->io_reads_.emplace_back(buffers_[0]);
                nodes_[1]->io_writes_.emplace_back(buffers_[1]);
                nodes_[2]->io_reads_.emplace_back(buffers_[1]);
                nodes_[2]->io_writes_.emplace_back(buffers_[2]);
            }

            /** @brief Returns the graph's problem over host_key(1) and host_key(2), each in a pool of its own. */
            [[nodiscard]] Result<GraphPartitionProblem> problem() const
            {
                const std::vector<PartitionDevice> devices{PartitionDevice{host_key(1), 1, "pcie"},
                                                           PartitionDevice{host_key(2), 2, "pcie"}};
                return make_graph_partition_problem(nodes_, memory_, directory_, devices);
            }

            /** @brief Returns the key of the device node was put on, or nothing when none was stated. */
            [[nodiscard]] std::optional<DeviceKey> device_of(std::size_t node) const
            {
                const std::optional<std::size_t> stated = nodes_[node]->stated_placement();
                if (!stated)
                    return std::nullopt;
                return *stated == 0 ? host_key(1) : host_key(2);
            }

            ResidencyDirectory directory_{0};
            InMemoryPlacementMemory memory_{{0, 0}};
            GlobalHandle buffers_[3];
            std::vector<sushi_ptr<Node>> nodes_ = std::vector<sushi_ptr<Node>>(3);
    };

    TEST_F(Small_PartitionApply, EachPlannedBufferIsFirstWrittenOnTheHomeTheAssignmentGivesIt)
    {
        const Result<GraphPartitionProblem> graph = problem();
        ASSERT_TRUE(graph.has_value());
        const ListSchedulingPartitioner partitioner(speed_model({1.0, 2.0}));
        const Result<PartitionAssignment, PartitionRefusal> answer = partitioner.partition(graph.value().problem);
        ASSERT_TRUE(answer.has_value());

        ASSERT_TRUE(apply_partition(graph.value(), answer.value()).has_value());

        // Node i is the first writer of buffer i, so its device holds the buffer's first instance.
        EXPECT_EQ(device_of(0), answer.value().home_of(buffers_[0]));
        EXPECT_EQ(device_of(1), answer.value().home_of(buffers_[1]));
        EXPECT_EQ(device_of(2), answer.value().home_of(buffers_[2]));
    }

    TEST_F(Small_PartitionApply, AnAssignmentForAnotherGraphIsRefusedAndChangesNoNode)
    {
        const Result<GraphPartitionProblem> graph = problem();
        ASSERT_TRUE(graph.has_value());
        PartitionAssignment other;
        other.node_devices = {host_key(1)};

        const Result<void> applied = apply_partition(graph.value(), other);

        ASSERT_FALSE(applied.has_value());
        EXPECT_EQ(applied.error().code, ErrorCode::InvalidArgument);
        EXPECT_FALSE(nodes_[0]->stated_placement().has_value());
    }

    TEST_F(Small_PartitionApply, ADeviceTheProblemDoesNotHoldIsRefusedAndChangesNoNode)
    {
        const Result<GraphPartitionProblem> graph = problem();
        ASSERT_TRUE(graph.has_value());
        PartitionAssignment stray;
        stray.node_devices = {host_key(1), host_key(1), host_key(9)};

        const Result<void> applied = apply_partition(graph.value(), stray);

        ASSERT_FALSE(applied.has_value());
        EXPECT_FALSE(nodes_[0]->stated_placement().has_value());
    }

    TEST_F(Small_PartitionApply, ASplitNodeGetsEachPartOnTheIndexOfItsDeviceWithItsByteRanges)
    {
        const Result<GraphPartitionProblem> graph = problem();
        ASSERT_TRUE(graph.has_value());
        PartitionAssignment cut;
        cut.node_devices = {host_key(1), host_key(2), host_key(1)};
        PlannedSplit split;
        split.node = 1;
        split.parts.push_back(PlannedSplitPart{host_key(2), SplitPart{0, 0, 4, 0, 2},
                                               {BufferRange{buffers_[0], 0, 300}}, {BufferRange{buffers_[1], 0, 100}}});
        split.parts.push_back(PlannedSplitPart{host_key(1), SplitPart{0, 4, 12, 1, 2},
                                               {BufferRange{buffers_[0], 0, 300}}, {BufferRange{buffers_[1], 100, 300}}});
        cut.splits.push_back(split);

        ASSERT_TRUE(apply_partition(graph.value(), cut).has_value());

        ASSERT_NE(nodes_[1]->placed_split, nullptr);
        EXPECT_EQ(nodes_[0]->placed_split, nullptr);
        const Internal::Graph::PlacedSplit& placed = *nodes_[1]->placed_split;
        ASSERT_EQ(placed.parts.size(), 2u);
        ASSERT_EQ(placed.access.size(), 2u);
        EXPECT_EQ(placed.parts[0].device, 1u);
        EXPECT_EQ(placed.parts[1].device, 0u);
        EXPECT_EQ(placed.parts[1].part, (SplitPart{0, 4, 12, 1, 2}));
        ASSERT_EQ(placed.access[1].writes.size(), 1u);
        EXPECT_EQ(placed.access[1].writes[0].offset, 100u);
        EXPECT_EQ(placed.access[1].writes[0].length, 200u);
        ASSERT_EQ(placed.access[0].reads.size(), 1u);
        EXPECT_EQ(placed.access[0].reads[0].length, 300u);
    }

    TEST_F(Small_PartitionApply, APartOnADeviceTheProblemDoesNotHoldIsRefusedAndChangesNoNode)
    {
        const Result<GraphPartitionProblem> graph = problem();
        ASSERT_TRUE(graph.has_value());
        PartitionAssignment cut;
        cut.node_devices = {host_key(1), host_key(2), host_key(1)};
        PlannedSplit split;
        split.node = 1;
        split.parts.push_back(PlannedSplitPart{host_key(2), SplitPart{0, 0, 4, 0, 2}, {}, {}});
        split.parts.push_back(PlannedSplitPart{host_key(9), SplitPart{0, 4, 12, 1, 2}, {}, {}});
        cut.splits.push_back(split);

        const Result<void> applied = apply_partition(graph.value(), cut);

        ASSERT_FALSE(applied.has_value());
        EXPECT_EQ(applied.error().code, ErrorCode::InvalidArgument);
        EXPECT_FALSE(nodes_[0]->stated_placement().has_value());
        EXPECT_EQ(nodes_[1]->placed_split, nullptr);
    }

    TEST_F(Small_PartitionApply, ANodeAnEarlierPartitionPlacedCountsAsFreeInTheNextProblem)
    {
        const Result<GraphPartitionProblem> first = problem();
        ASSERT_TRUE(first.has_value());
        PartitionAssignment answer;
        answer.node_devices = {host_key(1), host_key(2), host_key(1)};
        ASSERT_TRUE(apply_partition(first.value(), answer).has_value());

        const std::vector<PartitionDevice> devices{PartitionDevice{host_key(1), 1, "pcie"},
                                                   PartitionDevice{host_key(2), 2, "pcie"}};
        const std::set<const Node*> assigned{nodes_[0].get(), nodes_[1].get(), nodes_[2].get()};
        const Result<GraphPartitionProblem> again =
            make_graph_partition_problem(nodes_, memory_, directory_, devices, assigned);
        const Result<GraphPartitionProblem> stated = make_graph_partition_problem(nodes_, memory_, directory_, devices);
        ASSERT_TRUE(again.has_value());
        ASSERT_TRUE(stated.has_value());

        for (std::size_t node = 0; node < 3; ++node)
        {
            EXPECT_FALSE(again.value().problem.nodes[node].placed.has_value());
            EXPECT_TRUE(stated.value().problem.nodes[node].placed.has_value());
        }
    }
} // namespace
