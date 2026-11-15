/**************************************************************************/
/* test_plan_splice.cpp                                                   */
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
 * @file test_plan_splice.cpp
 * @brief Tests that splice_plan adds the planned nodes with the edges that order them.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/logical_time_eviction.hpp>
#include <SushiRuntime/coherence/plan_splice.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::MemoryProbe;
using SushiRuntime::Internal::Memory::MemorySpace;
using SushiRuntime::Internal::Memory::ProbedMemory;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    constexpr std::size_t K_BYTES = 16;

    /** @brief Builds a plain node per step, labelled by the step's kind, and keeps the steps. */
    class RecordingFactory final : public PlannedNodeFactory
    {
        public:
            [[nodiscard]] Result<sushi_ptr<Node>> make(const PlannedStep& step) override
            {
                steps.push_back(step);
                sushi_ptr<Node> node = make_sushi<Node>();
                node->metadata.name = step.kind == PlannedStepKind::Copy ? "copy"
                                    : step.kind == PlannedStepKind::Free ? "free" : "allocate";
                return node;
            }

            std::vector<PlannedStep> steps; /**< Steps built, in order. */
    };

    /** @brief Reports a pool of K_BYTES bytes, all of it free. */
    class OneBufferProbe final : public MemoryProbe
    {
        public:
            [[nodiscard]] ProbedMemory probe() const override
            {
                return ProbedMemory{K_BYTES, K_BYTES};
            }
    };

    /** @brief Returns whether to is a successor of from. */
    bool precedes(const Node& from, const Node* to)
    {
        bool found = false;
        from.for_each_successor([&found, to](Node* successor) { found = found || successor == to; });
        return found;
    }

    /** @brief Adds from -> to as a built graph holds it. */
    void link(Node& from, Node& to)
    {
        from.add_successor(&to);
        to.in_degree.fetch_add(1);
    }

    /** @brief Holds a buffer on device 0, a writer on device 0 and a reader that writes on device 1. */
    class Small_PlanSplice : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                for (const MemorySpaceID id : {K_DEVICE_0, K_DEVICE_1})
                    owned_.push_back(std::make_unique<DeviceMemorySpace>(
                        DeviceMemorySpaceDescription{id, id.value(), {}}, probe_));
                buffer_ = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
                ASSERT_EQ(directory_.describe(buffer_, K_BYTES, ElementLayout{}, K_DEVICE_0), HandleStatus::Ok);
                ASSERT_EQ(directory_.set_instance(buffer_, K_DEVICE_0, &bytes_), HandleStatus::Ok);

                writer_->io_writes_.emplace_back(buffer_);
                reader_->io_reads_.emplace_back(buffer_);
                reader_->io_writes_.emplace_back(buffer_);
                link(*writer_, *reader_);
                nodes_ = {writer_, reader_};
                spaces_[writer_.get()] = K_DEVICE_0;
                spaces_[reader_.get()] = K_DEVICE_1;
            }

            /** @brief Returns the space each node was given. */
            SpaceOfNode space_of() const
            {
                return [this](const Node& node) -> std::optional<MemorySpaceID>
                {
                    const auto found = spaces_.find(&node);
                    if (found == spaces_.end())
                        return std::nullopt;
                    return found->second;
                };
            }

            ResidencyDirectory directory_{0};
            FixedMemoryProbe probe_;
            std::vector<std::unique_ptr<MemorySpace>> owned_;
            FakeTransferPath path_;
            GlobalHandle buffer_;
            unsigned char bytes_[K_BYTES] = {};
            sushi_ptr<Node> writer_ = make_sushi<Node>();
            sushi_ptr<Node> reader_ = make_sushi<Node>();
            std::vector<sushi_ptr<Node>> nodes_;
            std::map<const Node*, MemorySpaceID> spaces_;
    };

    TEST_F(Small_PlanSplice, TheCopyRunsAfterTheWriterAndBeforeTheReaderOnTheOtherDevice)
    {
        CoherencePlanner planner(directory_, {owned_[0].get(), owned_[1].get()}, {&path_},
                                 uniform_transfer_cost());
        RecordingFactory factory;

        ASSERT_TRUE(splice_plan(nodes_, space_of(), planner, factory));

        // Allocate and copy before the reader, then the copy back and the free at the end.
        ASSERT_EQ(nodes_.size(), 6U);
        Node* const allocate = nodes_[2].get();
        Node* const copy = nodes_[3].get();
        Node* const copy_back = nodes_[4].get();
        Node* const free = nodes_[5].get();
        EXPECT_STREQ(copy->metadata.name, "copy");
        EXPECT_TRUE(precedes(*writer_, copy));
        EXPECT_TRUE(precedes(*allocate, copy));
        EXPECT_TRUE(precedes(*copy, reader_.get()));
        EXPECT_EQ(reader_->in_degree.load(), 3);
        EXPECT_TRUE(precedes(*reader_, copy_back));
        EXPECT_TRUE(precedes(*copy_back, free));
        EXPECT_EQ(factory.steps[1].source, K_DEVICE_0);
        EXPECT_EQ(factory.steps[1].space, K_DEVICE_1);
    }

    TEST_F(Small_PlanSplice, NodesOnOneDeviceAreLeftAsTheyAre)
    {
        spaces_[reader_.get()] = K_DEVICE_0;
        CoherencePlanner planner(directory_, {owned_[0].get(), owned_[1].get()}, {&path_},
                                 uniform_transfer_cost());
        RecordingFactory factory;

        ASSERT_TRUE(splice_plan(nodes_, space_of(), planner, factory));

        EXPECT_EQ(nodes_.size(), 2U);
        EXPECT_EQ(reader_->in_degree.load(), 1);
    }

    TEST_F(Small_PlanSplice, AnEvictionRunsAfterTheNodesThatUsedTheInstanceAndBeforeTheAllocationItMakesRoomFor)
    {
        OneBufferProbe one_buffer;
        owned_[1] = std::make_unique<DeviceMemorySpace>(
            DeviceMemorySpaceDescription{K_DEVICE_1, K_DEVICE_1.value(), {}}, one_buffer);
        const GlobalHandle other = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
        unsigned char other_bytes[K_BYTES] = {};
        ASSERT_EQ(directory_.describe(other, K_BYTES, ElementLayout{}, K_DEVICE_0), HandleStatus::Ok);
        ASSERT_EQ(directory_.set_instance(other, K_DEVICE_0, &other_bytes), HandleStatus::Ok);
        sushi_ptr<Node> third = make_sushi<Node>();
        third->io_reads_.emplace_back(other);
        nodes_.push_back(third);
        spaces_[third.get()] = K_DEVICE_1;

        LogicalTimeEviction policy;
        CoherencePlanner planner(directory_, {owned_[0].get(), owned_[1].get()}, {&path_},
                                 uniform_transfer_cost());
        ResidencyOptions options;
        options.policy = &policy;
        options.host = K_DEVICE_0;
        planner.set_residency(options);
        RecordingFactory factory;

        ASSERT_TRUE(splice_plan(nodes_, space_of(), planner, factory));

        // The reader's buffer is the only copy on device 1, so it is written back and freed first.
        ASSERT_EQ(nodes_.size(), 9U);
        Node* const write_back = nodes_[5].get();
        Node* const free = nodes_[6].get();
        Node* const allocate_other = nodes_[7].get();
        EXPECT_STREQ(write_back->metadata.name, "copy");
        EXPECT_STREQ(free->metadata.name, "free");
        EXPECT_STREQ(allocate_other->metadata.name, "allocate");
        EXPECT_TRUE(precedes(*reader_, write_back));
        EXPECT_TRUE(precedes(*write_back, free));
        EXPECT_TRUE(precedes(*free, allocate_other));
        EXPECT_TRUE(precedes(*free, third.get()));
    }

    TEST_F(Small_PlanSplice, ARefusedPlanLeavesTheNodesUnchanged)
    {
        ClosedTransferPath closed;
        CoherencePlanner planner(directory_, {owned_[0].get(), owned_[1].get()}, {&closed},
                                 uniform_transfer_cost());
        RecordingFactory factory;

        EXPECT_FALSE(splice_plan(nodes_, space_of(), planner, factory));
        EXPECT_EQ(nodes_.size(), 2U);
        EXPECT_EQ(reader_->in_degree.load(), 1);
    }
} // namespace
