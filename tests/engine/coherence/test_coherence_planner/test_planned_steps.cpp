/**************************************************************************/
/* test_planned_steps.cpp                                                 */
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
 * @file test_planned_steps.cpp
 * @brief Tests the copies, allocations and frees CoherencePlanner plans, and its choice of source.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "support/fake_transfer_path.hpp"

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
                return CoherencePlanner(directory_, directory_, spaces({0, 1, 2}), {&path_}, std::move(cost));
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

        CoherencePlanner by_id(directory_, directory_, spaces({0, 1, 2}), {&path_}, uniform_transfer_cost());
        CoherencePlanner by_other_key(directory_, directory_, spaces({1, 0, 2}), {&path_}, uniform_transfer_cost());
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
        CoherencePlanner subject(directory_, directory_, spaces({0, 1, 2}), {&closed}, uniform_transfer_cost());

        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_1, {buffer_}, {})});
        ASSERT_FALSE(plan);
        EXPECT_EQ(plan.error().code, ErrorCode::Unsupported);
    }

    TEST_F(Small_CoherencePlanner, EachPartOfASplitNodeHoldsItsReadsAndItsWrittenRangeComesBackToALaterReader)
    {
        NodeAccess split = node(K_DEVICE_0, {}, {});
        split.parts.push_back(NodeAccessPart{K_DEVICE_0, {BufferRegion(buffer_)}, {BufferRegion(buffer_, 0, 32)}});
        split.parts.push_back(NodeAccessPart{K_DEVICE_1, {BufferRegion(buffer_)}, {BufferRegion(buffer_, 32, 32)}});
        CoherencePlanner subject = planner();

        const Result<CopyPlan> plan = subject.plan({split, node(K_DEVICE_0, {buffer_}, {})});
        ASSERT_TRUE(plan);

        const auto has_copy = [&plan](MemorySpaceID source, MemorySpaceID destination, std::size_t begin,
                                      std::size_t end, std::size_t before)
        {
            for (const PlannedStep& step : plan.value().steps)
                if (step.kind == PlannedStepKind::Copy && step.source == source && step.space == destination &&
                    step.begin == begin && step.end == end && step.node == before)
                    return true;
            return false;
        };
        // The part on device 1 reads the whole buffer, which device 0 alone held before the node.
        EXPECT_TRUE(has_copy(K_DEVICE_0, K_DEVICE_1, 0, K_BYTES, 0));
        // The range that part wrote comes back to device 0 for the reader after it, and nothing else does.
        EXPECT_TRUE(has_copy(K_DEVICE_1, K_DEVICE_0, 32, K_BYTES, 1));
        EXPECT_FALSE(has_copy(K_DEVICE_1, K_DEVICE_0, 0, K_BYTES, 1));
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

    /** @brief The FNV-1a 64-bit offset basis. */
    constexpr std::uint64_t K_FNV_OFFSET = 14695981039346656037ULL;

    /** @brief The FNV-1a 64-bit prime. */
    constexpr std::uint64_t K_FNV_PRIME = 1099511628211ULL;

    /** @brief Folds one 64-bit word into an FNV-1a hash, one byte at a time, lowest byte first. */
    std::uint64_t fold_word(std::uint64_t hash, std::uint64_t word) noexcept
    {
        for (int shift = 0; shift < 64; shift += 8)
            hash = (hash ^ ((word >> shift) & 0xFFU)) * K_FNV_PRIME;
        return hash;
    }

    /**
     * @brief Returns a canonical hash of a plan: its step count, then every field of each step in
     *        declaration order.
     */
    std::uint64_t plan_hash(const CopyPlan& plan) noexcept
    {
        std::uint64_t hash = fold_word(K_FNV_OFFSET, plan.steps.size());
        for (const PlannedStep& step : plan.steps)
        {
            hash = fold_word(hash, static_cast<std::uint64_t>(step.kind));
            hash = fold_word(hash, step.handle.to_high_word());
            hash = fold_word(hash, step.handle.to_low_word());
            hash = fold_word(hash, step.space.value());
            hash = fold_word(hash, step.source.value());
            hash = fold_word(hash, step.begin);
            hash = fold_word(hash, step.end);
            hash = fold_word(hash, step.alignment);
            hash = fold_word(hash, step.path);
            hash = fold_word(hash, step.node);
        }
        return hash;
    }

    TEST_F(Small_CoherencePlanner, ThePlanOfAMultiDeviceGraphHashesEqualAcrossRuns)
    {
        // A second buffer, written on device 1, so the graph moves two buffers across three devices.
        const GlobalHandle second = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
        ASSERT_EQ(directory_.describe(second, K_BYTES, ElementLayout{4, 4}, K_DEVICE_1), HandleStatus::Ok);
        ASSERT_EQ(directory_.set_instance(second, K_DEVICE_1, &device_1_bytes_), HandleStatus::Ok);
        const std::vector<NodeAccess> graph{node(K_DEVICE_0, {}, {buffer_}),
                                            node(K_DEVICE_1, {buffer_}, {second}),
                                            node(K_DEVICE_2, {second, buffer_}, {}),
                                            node(K_DEVICE_0, {second}, {buffer_})};

        // Each run plans with a planner of its own, as a run of the program would.
        const Result<CopyPlan> first = planner().plan(graph);
        const Result<CopyPlan> again = planner().plan(graph);
        const Result<CopyPlan> last = planner().plan(graph);
        ASSERT_TRUE(first && again && last);
        ASSERT_FALSE(first.value().steps.empty());
        EXPECT_EQ(plan_hash(first.value()), plan_hash(again.value()));
        EXPECT_EQ(plan_hash(first.value()), plan_hash(last.value()));
        EXPECT_EQ(first.value().steps, again.value().steps);
    }

    TEST(Small_CoherencePlannerByteRange, ClipsARegionToTheBuffer)
    {
        const GlobalHandle handle;
        EXPECT_EQ(byte_range(BufferRegion(handle, 8, BufferRegion::K_WHOLE), 32), std::make_pair(std::size_t{8}, std::size_t{32}));
        EXPECT_EQ(byte_range(BufferRegion(handle, 40, 4), 32), std::make_pair(std::size_t{32}, std::size_t{32}));
    }
    /** @brief Applies to the directory's maps what a run of plan over node_count nodes records, in run order. */
    void record_run(ResidencyDirectory& directory, const CopyPlan& plan, std::size_t node_count)
    {
        const auto record = [&directory, &plan](std::size_t node)
        {
            for (const PlannedStep& step : plan.steps)
            {
                if (step.node != node)
                    continue;
                Internal::Memory::LockedCoherence map = directory.coherence(step.handle);
                if (step.kind == PlannedStepKind::Copy)
                    map->record_copy(step.begin, step.end, step.space);
                else if (step.kind == PlannedStepKind::Free)
                    map->drop_space(step.space);
            }
            for (const PlannedEffect& effect : plan.effects)
            {
                if (effect.node != node)
                    continue;
                Internal::Memory::LockedCoherence map = directory.coherence(effect.handle);
                if (effect.kind == PlannedEffectKind::Hold)
                    map.enter_run();
                else if (effect.kind == PlannedEffectKind::Leave)
                    map.leave_run();
                else
                    map->record_write(effect.begin, effect.end, effect.space);
            }
        };
        for (std::size_t node = 0; node < node_count; ++node)
            record(node);
        record(PlannedStep::K_GRAPH_END);
    }

    /** @brief Runs the planner from several threads at once on graphs that share one buffer. */
    class Medium_CoherencePlanner : public Small_CoherencePlanner
    {
    };

    TEST_F(Medium_CoherencePlanner, TwoCommitsAtOnceOnASharedBufferAndAHorizonLeaveOneWholeMap)
    {
        constexpr int K_ROUNDS = 2000;
        std::atomic<int> refused{0};
        const auto commit_and_run = [this, &refused](MemorySpaceID space)
        {
            CoherencePlanner subject = planner();
            const std::vector<NodeAccess> graph{node(space, {buffer_}, {buffer_})};
            for (int round = 0; round < K_ROUNDS; ++round)
            {
                const Result<CopyPlan> plan = subject.plan(graph);
                if (!plan)
                {
                    refused.fetch_add(1);
                    continue;
                }
                record_run(directory_, plan.value(), graph.size());
            }
        };
        std::atomic<bool> done{false};
        std::thread first(commit_and_run, K_DEVICE_1);
        std::thread second(commit_and_run, K_DEVICE_2);
        std::thread horizons([this, &done]()
        {
            while (!done.load())
                static_cast<void>(directory_.retire_coherence_history());
        });
        first.join();
        second.join();
        done.store(true);
        horizons.join();

        EXPECT_EQ(refused.load(), 0);
        Internal::Memory::LockedCoherence map = directory_.coherence(buffer_);
        ASSERT_TRUE(map != nullptr);
        EXPECT_FALSE(map.held_by_run());
        std::size_t next = 0;
        for (const Internal::Memory::CoherenceInterval& interval : map->intervals())
        {
            EXPECT_EQ(interval.begin, next);
            EXPECT_NE(interval.valid_spaces, 0U);
            next = interval.end;
        }
        EXPECT_EQ(next, K_BYTES);
    }
} // namespace
