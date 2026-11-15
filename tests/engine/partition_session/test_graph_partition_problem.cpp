/**************************************************************************/
/* test_graph_partition_problem.cpp                                       */
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
 * @file test_graph_partition_problem.cpp
 * @brief Tests that make_graph_partition_problem() reads a graph's buffers, edges and fixed nodes.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

#include <SushiRuntime/machine/node_split.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/partition_session/graph_partition_problem.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Graph::InMemoryPlacementMemory;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    /** @brief Returns a handle of directory with flags, described as bytes long on the host space. */
    GlobalHandle described_handle(ResidencyDirectory& directory, std::size_t bytes,
                                  HandleFlags flags = HandleFlags::None)
    {
        const GlobalHandle handle = directory.mint(HandleKind::Buffer, flags).handle;
        static_cast<void>(directory.describe(handle, bytes, ElementLayout{}, MemorySpaceID(0)));
        return handle;
    }

    /** @brief Returns two devices, host_key(1) and host_key(2), each in a pool of its own. */
    std::vector<PartitionDevice> two_devices()
    {
        return {PartitionDevice{host_key(1), 1, "pcie"}, PartitionDevice{host_key(2), 2, "pcie"}};
    }

    /** @brief Holds a writer of a planned buffer and a reader that writes a buffer device 1 owns. */
    class Small_GraphPartitionProblem : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                planned_ = described_handle(directory_, 64);
                owned_ = described_handle(directory_, 32);
                memory_.record(owned_, 1);
                writer_->io_writes_.emplace_back(planned_);
                reader_->io_reads_.emplace_back(planned_);
                reader_->io_writes_.emplace_back(owned_);
                nodes_ = {writer_, reader_};
            }

            ResidencyDirectory directory_{0};
            InMemoryPlacementMemory memory_{{0, 0}};
            GlobalHandle planned_;
            GlobalHandle owned_;
            sushi_ptr<Node> writer_ = make_sushi<Node>();
            sushi_ptr<Node> reader_ = make_sushi<Node>();
            std::vector<sushi_ptr<Node>> nodes_;
    };

    TEST_F(Small_GraphPartitionProblem, ABufferADeviceOwnsKeepsItAndAnyOtherIsPlanned)
    {
        const Result<GraphPartitionProblem> built =
            make_graph_partition_problem(nodes_, memory_, directory_, two_devices());

        ASSERT_TRUE(built.has_value());
        ASSERT_EQ(built.value().problem.buffers.size(), 2u);
        EXPECT_FALSE(built.value().problem.buffers[0].device.has_value());
        EXPECT_EQ(built.value().problem.buffers[0].bytes, 64u);
        EXPECT_EQ(built.value().problem.buffers[1].device, host_key(2));
        EXPECT_EQ(built.value().planned_buffers, 1u);
    }

    TEST_F(Small_GraphPartitionProblem, AReaderWaitsOnTheLastWriterForTheBytesItReads)
    {
        const Result<GraphPartitionProblem> built =
            make_graph_partition_problem(nodes_, memory_, directory_, two_devices());

        ASSERT_TRUE(built.has_value());
        ASSERT_EQ(built.value().problem.edges.size(), 1u);
        EXPECT_EQ(built.value().problem.edges[0].from, 0u);
        EXPECT_EQ(built.value().problem.edges[0].to, 1u);
        EXPECT_EQ(built.value().problem.edges[0].bytes, 64u);
    }

    TEST_F(Small_GraphPartitionProblem, AFixedToHomeNodeWhoseAnchorADeviceOwnsStaysOnThatDevice)
    {
        const Result<GraphPartitionProblem> built =
            make_graph_partition_problem(nodes_, memory_, directory_, two_devices());

        ASSERT_TRUE(built.has_value());
        EXPECT_FALSE(built.value().problem.nodes[0].placed.has_value());
        EXPECT_EQ(built.value().problem.nodes[1].placed, host_key(2));
    }

    TEST_F(Small_GraphPartitionProblem, ANodeThatDeclaresASplitCarriesItsAxisAndExtent)
    {
        auto split = std::make_shared<NodeSplit>();
        split->axis = 2;
        split->extent = 96;
        writer_->declared_split = split;

        const Result<GraphPartitionProblem> built =
            make_graph_partition_problem(nodes_, memory_, directory_, two_devices());

        ASSERT_TRUE(built.has_value());
        ASSERT_TRUE(built.value().problem.nodes[0].split.has_value());
        EXPECT_EQ(built.value().problem.nodes[0].split->axis, 2u);
        EXPECT_EQ(built.value().problem.nodes[0].split->extent, 96u);
        EXPECT_FALSE(built.value().problem.nodes[1].split.has_value());
    }

    TEST_F(Small_GraphPartitionProblem, ABorrowedBufferIsLeftOut)
    {
        const GlobalHandle borrowed = described_handle(directory_, 16, HandleFlags::Borrowed);
        writer_->io_reads_.emplace_back(borrowed);

        const Result<GraphPartitionProblem> built =
            make_graph_partition_problem(nodes_, memory_, directory_, two_devices());

        ASSERT_TRUE(built.has_value());
        EXPECT_EQ(built.value().problem.buffers.size(), 2u);
        EXPECT_TRUE(built.value().problem.nodes[0].reads.empty());
    }
} // namespace
