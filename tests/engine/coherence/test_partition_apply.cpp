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
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>
#include <SushiRuntime/coherence/partition_apply.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>

#include "partition_problem_builder.hpp"

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
} // namespace
