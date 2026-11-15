/**************************************************************************/
/* test_coherence_planner.cpp                                             */
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
 * @file test_coherence_planner.cpp
 * @brief Tests the copies, allocations and frees CoherencePlanner plans, and its choice of source.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::MemorySpace;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    const MemorySpaceID K_DEVICE_2(3);
    constexpr std::size_t K_BYTES = 64;

    /** @brief Holds a directory with one buffer on device 0, three device spaces and a fake path. */
    class Small_CoherencePlanner : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                for (const MemorySpaceID id : {K_DEVICE_0, K_DEVICE_1, K_DEVICE_2})
                    owned_.push_back(std::make_unique<DeviceMemorySpace>(
                        DeviceMemorySpaceDescription{id, id.value(), {}}, probe_));
                buffer_ = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
                ASSERT_EQ(directory_.describe(buffer_, K_BYTES, ElementLayout{4, 4}, K_DEVICE_0), HandleStatus::Ok);
                ASSERT_EQ(directory_.set_instance(buffer_, K_DEVICE_0, &device_0_bytes_), HandleStatus::Ok);
            }

            /** @brief Returns the spaces in the order given by indices into owned_. */
            std::vector<const MemorySpace*> spaces(std::vector<std::size_t> order) const
            {
                std::vector<const MemorySpace*> result;
                for (std::size_t index : order)
                    result.push_back(owned_[index].get());
                return result;
            }

            /** @brief Returns a planner over every space in id order and the fake path. */
            CoherencePlanner planner(TransferCost cost = uniform_transfer_cost())
            {
                return CoherencePlanner(directory_, spaces({0, 1, 2}), {&path_}, std::move(cost));
            }

            /** @brief Returns a node on space that reads and writes the buffers named. */
            NodeAccess node(MemorySpaceID space, std::vector<GlobalHandle> reads,
                            std::vector<GlobalHandle> writes) const
            {
                NodeAccess access;
                access.planned = true;
                access.space = space;
                for (GlobalHandle handle : reads)
                    access.reads.emplace_back(handle);
                for (GlobalHandle handle : writes)
                    access.writes.emplace_back(handle);
                return access;
            }

            /** @brief Returns a copy step of the whole buffer. */
            PlannedStep copy(MemorySpaceID source, MemorySpaceID destination, std::size_t before) const
            {
                PlannedStep step;
                step.kind = PlannedStepKind::Copy;
                step.handle = buffer_;
                step.space = destination;
                step.source = source;
                step.end = K_BYTES;
                step.node = before;
                return step;
            }

            ResidencyDirectory directory_{0};
            FixedMemoryProbe probe_;
            std::vector<std::unique_ptr<MemorySpace>> owned_;
            FakeTransferPath path_;
            GlobalHandle buffer_;
            unsigned char device_0_bytes_[K_BYTES] = {};
            unsigned char device_1_bytes_[K_BYTES] = {};
    };

    TEST_F(Small_CoherencePlanner, ANodeOnDevice1ReadingABufferWrittenOnDevice0GetsAPlannedCopyAndItsWriteInvalidatesDevice0)
    {
        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_0, {}, {buffer_}),
                                                    node(K_DEVICE_1, {buffer_}, {buffer_}),
                                                    node(K_DEVICE_0, {buffer_}, {})});
        ASSERT_TRUE(plan);

        PlannedStep allocate;
        allocate.kind = PlannedStepKind::Allocate;
        allocate.handle = buffer_;
        allocate.space = K_DEVICE_1;
        allocate.end = K_BYTES;
        allocate.alignment = 4;
        allocate.node = 1;
        PlannedStep free = allocate;
        free.kind = PlannedStepKind::Free;
        free.alignment = 1;
        free.node = PlannedStep::K_GRAPH_END;

        const std::vector<PlannedStep> expected{allocate, copy(K_DEVICE_0, K_DEVICE_1, 1),
                                                copy(K_DEVICE_1, K_DEVICE_0, 2), free};
        EXPECT_EQ(plan.value().steps, expected);
    }

    TEST_F(Small_CoherencePlanner, ANodeOnTheDeviceThatHoldsTheBytesGetsNoStep)
    {
        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_0, {buffer_}, {buffer_})});
        ASSERT_TRUE(plan);
        EXPECT_TRUE(plan.value().steps.empty());
    }

    TEST_F(Small_CoherencePlanner, TheFirstValidSpaceInDeviceKeyOrderIsTheSourceOfAnEqualCost)
    {
        ASSERT_EQ(directory_.set_instance(buffer_, K_DEVICE_1, &device_1_bytes_), HandleStatus::Ok);
        directory_.coherence(buffer_)->reset(K_BYTES, K_DEVICE_0);
        directory_.coherence(buffer_)->record_copy(0, K_BYTES, K_DEVICE_1);

        CoherencePlanner by_id(directory_, spaces({0, 1, 2}), {&path_}, uniform_transfer_cost());
        CoherencePlanner by_other_key(directory_, spaces({1, 0, 2}), {&path_}, uniform_transfer_cost());
        const std::vector<NodeAccess> nodes{node(K_DEVICE_2, {buffer_}, {})};

        const Result<CopyPlan> first = by_id.plan(nodes);
        const Result<CopyPlan> again = by_id.plan(nodes);
        const Result<CopyPlan> other = by_other_key.plan(nodes);
        ASSERT_TRUE(first && again && other);
        ASSERT_EQ(first.value().steps.size(), 3U);
        EXPECT_EQ(first.value().steps[1].source, K_DEVICE_0);
        EXPECT_EQ(other.value().steps[1].source, K_DEVICE_1);
        EXPECT_EQ(first.value().steps, again.value().steps);
    }

    TEST_F(Small_CoherencePlanner, TheCheapestPathChoosesTheSource)
    {
        ASSERT_EQ(directory_.set_instance(buffer_, K_DEVICE_1, &device_1_bytes_), HandleStatus::Ok);
        directory_.coherence(buffer_)->reset(K_BYTES, K_DEVICE_0);
        directory_.coherence(buffer_)->record_copy(0, K_BYTES, K_DEVICE_1);
        CoherencePlanner subject = planner([](const TransferPath&, MemorySpaceID source, MemorySpaceID,
                                              std::size_t)
        {
            return source == K_DEVICE_1 ? 1.0 : 2.0;
        });

        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_2, {buffer_}, {})});
        ASSERT_TRUE(plan);
        EXPECT_EQ(plan.value().steps[1].source, K_DEVICE_1);
    }

    TEST_F(Small_CoherencePlanner, ACopyNoPathMakesIsRefusedAsUnsupported)
    {
        ClosedTransferPath closed;
        CoherencePlanner subject(directory_, spaces({0, 1, 2}), {&closed}, uniform_transfer_cost());

        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_1, {buffer_}, {})});
        ASSERT_FALSE(plan);
        EXPECT_EQ(plan.error().code, ErrorCode::Unsupported);
    }

    TEST_F(Small_CoherencePlanner, ANodeThePlannerLeavesAloneGetsNoStep)
    {
        NodeAccess skipped = node(K_DEVICE_1, {buffer_}, {});
        skipped.planned = false;
        CoherencePlanner subject = planner();

        const Result<CopyPlan> plan = subject.plan({skipped});
        ASSERT_TRUE(plan);
        EXPECT_TRUE(plan.value().steps.empty());
    }

    TEST(Small_CoherencePlannerByteRange, ClipsARegionToTheBuffer)
    {
        const GlobalHandle handle;
        EXPECT_EQ(byte_range(BufferRegion(handle, 8, BufferRegion::K_WHOLE), 32), std::make_pair(std::size_t{8}, std::size_t{32}));
        EXPECT_EQ(byte_range(BufferRegion(handle, 40, 4), 32), std::make_pair(std::size_t{32}, std::size_t{32}));
    }
} // namespace
