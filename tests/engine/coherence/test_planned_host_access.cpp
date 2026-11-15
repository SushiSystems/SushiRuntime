/**************************************************************************/
/* test_planned_host_access.cpp                                           */
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
 * @file test_planned_host_access.cpp
 * @brief Tests PlannedHostAccess and the coherence planner on planned buffers, on fakes.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/logical_time_eviction.hpp>
#include <SushiRuntime/coherence/planned_host_access.hpp>
#include <SushiRuntime/coherence/residency_options.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "support/fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::HostMemorySpace;
using SushiRuntime::Internal::Memory::HostMemorySpaceDescription;
using SushiRuntime::Internal::Memory::MemorySpace;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_HOST(0);
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    constexpr std::size_t K_COUNT = 16;
    constexpr std::size_t K_BYTES = K_COUNT * sizeof(std::int32_t);

    /** @brief Hands out heap memory and counts what is still held. */
    class HeapAllocator final : public InstanceAllocator
    {
        public:
            [[nodiscard]] void* allocate(MemorySpaceID, std::size_t bytes, std::size_t) override
            {
                ++held;
                return std::malloc(bytes);
            }

            void release(MemorySpaceID, void* address) noexcept override
            {
                --held;
                std::free(address);
            }

            /** @brief Returns Host: the directory records a kind with each instance this allocator made. */
            [[nodiscard]] std::optional<Internal::Memory::USMKind> kind_of(MemorySpaceID) const noexcept override
            {
                return Internal::Memory::USMKind::Host;
            }

            int held = 0; /**< Allocations not yet released. */
    };

    /** @brief Holds a directory, a host and two device spaces, a memcpy path and the access. */
    class Small_PlannedHostAccess : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                owned_.push_back(std::make_unique<HostMemorySpace>(
                    HostMemorySpaceDescription{K_HOST, 0, 0, {}}, probe_));
                for (const MemorySpaceID id : {K_DEVICE_0, K_DEVICE_1})
                    owned_.push_back(std::make_unique<DeviceMemorySpace>(
                        DeviceMemorySpaceDescription{id, id.value(), {}}, probe_));
                for (const std::unique_ptr<MemorySpace>& space : owned_)
                    spaces_.push_back(space.get());
                paths_.push_back(&path_);
            }

            /** @brief Returns a planner over every space and the fake path. */
            CoherencePlanner planner()
            {
                return CoherencePlanner(directory_, spaces_, {&path_}, uniform_transfer_cost());
            }

            /** @brief Returns a node on space that reads and writes the buffers named. */
            static NodeAccess node(MemorySpaceID space, std::vector<GlobalHandle> reads,
                                   std::vector<GlobalHandle> writes)
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

            /** @brief Returns the address of handle's instance on space, or null. */
            void* instance(GlobalHandle handle, MemorySpaceID space) const
            {
                const ResolveOutcome outcome = directory_.resolve(handle, space);
                return outcome.status == HandleStatus::Ok ? outcome.address : nullptr;
            }

            /** @brief Runs the steps of a plan that are ordered before node. */
            void run_steps_before(const CopyPlan& plan, std::size_t node)
            {
                for (const PlannedStep& step : plan.steps)
                {
                    if (step.node != node)
                        continue;
                    run(step);
                }
            }

            /** @brief Runs one planned step on the heap allocator and the fake path. */
            void run(const PlannedStep& step)
            {
                switch (step.kind)
                {
                    case PlannedStepKind::Allocate:
                    {
                        void* const address = allocator_.allocate(step.space, step.end, step.alignment);
                        ASSERT_EQ(directory_.set_instance(step.handle, step.space, address, allocator_.kind_of(step.space)),
                                  HandleStatus::Ok);
                        break;
                    }
                    case PlannedStepKind::Copy:
                        std::memcpy(static_cast<unsigned char*>(instance(step.handle, step.space)) + step.begin,
                                    static_cast<unsigned char*>(instance(step.handle, step.source)) + step.begin,
                                    step.end - step.begin);
                        break;
                    case PlannedStepKind::Free:
                    {
                        void* const address = instance(step.handle, step.space);
                        ASSERT_EQ(directory_.drop_instance(step.handle, step.space), HandleStatus::Ok);
                        allocator_.release(step.space, address);
                        break;
                    }
                }
            }

            /** @brief Returns K_COUNT values, value i being first + i. */
            static std::vector<std::int32_t> pattern(std::int32_t first)
            {
                std::vector<std::int32_t> values(K_COUNT);
                for (std::size_t index = 0; index < K_COUNT; ++index)
                    values[index] = first + static_cast<std::int32_t>(index);
                return values;
            }

            ResidencyDirectory directory_{0};
            FixedMemoryProbe probe_;
            std::vector<std::unique_ptr<MemorySpace>> owned_;
            std::vector<const MemorySpace*> spaces_;
            FakeTransferPath path_;
            std::vector<TransferPath*> paths_;
            HeapAllocator allocator_;
            PlannedHostAccess access_{directory_, spaces_, paths_, allocator_, K_HOST};
    };

    TEST_F(Small_PlannedHostAccess, APlannedBufferIsMintedWithItsSizeAndNoInstance)
    {
        const Result<GlobalHandle> handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4});
        ASSERT_TRUE(handle);

        const Internal::Memory::EntryView entry = directory_.inspect(*handle);
        EXPECT_EQ(entry.status, HandleStatus::Ok);
        EXPECT_EQ(entry.size, K_BYTES);
        EXPECT_EQ(entry.valid_spaces, 0U);
        EXPECT_EQ(directory_.coherence(*handle)->size(), K_BYTES);
        EXPECT_EQ(directory_.coherence(*handle)->valid_spaces_at(0), 0U);
        EXPECT_EQ(allocator_.held, 0);
        access_.release(*handle);
    }

    TEST_F(Small_PlannedHostAccess, APlannedBufferWithNoInstanceIsAllocatedOnTheFirstNodeNotRefused)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();

        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_1, {}, {handle})});
        ASSERT_TRUE(plan);
        ASSERT_FALSE(plan.value().steps.empty());
        EXPECT_EQ(plan.value().steps.front().kind, PlannedStepKind::Allocate);
        EXPECT_EQ(plan.value().steps.front().space, K_DEVICE_1);
        for (const PlannedStep& step : plan.value().steps)
            EXPECT_NE(step.kind, PlannedStepKind::Copy);
        access_.release(handle);
    }

    TEST_F(Small_PlannedHostAccess, APlannedBufferWrittenBeforeAPlanReadsBackEqualAfterANodeOnAnotherDeviceWroteIt)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();
        const std::vector<std::int32_t> before = pattern(100);
        ASSERT_TRUE(access_.write(handle, 0, before.data(), K_BYTES));
        EXPECT_EQ(directory_.coherence(handle)->valid_spaces_at(0), std::uint64_t{1} << K_HOST.value());

        LogicalTimeEviction policy;
        CoherencePlanner subject = planner();
        ResidencyOptions options;
        options.policy = &policy;
        options.host = K_HOST;
        subject.set_residency(options);
        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_1, {handle}, {handle})});
        ASSERT_TRUE(plan);

        run_steps_before(plan.value(), 0);
        auto* const device_bytes = static_cast<std::int32_t*>(instance(handle, K_DEVICE_1));
        ASSERT_NE(device_bytes, nullptr);
        EXPECT_EQ(std::vector<std::int32_t>(device_bytes, device_bytes + K_COUNT), before);
        const std::vector<std::int32_t> written = pattern(700);
        std::memcpy(device_bytes, written.data(), K_BYTES);
        run_steps_before(plan.value(), PlannedStep::K_GRAPH_END);

        std::vector<std::int32_t> read(K_COUNT);
        ASSERT_TRUE(access_.read(handle, 0, read.data(), K_BYTES));
        EXPECT_EQ(read, written);
        EXPECT_EQ(path_.copies(), 1);
        access_.release(handle);
        EXPECT_EQ(allocator_.held, 0);
    }

    TEST_F(Small_PlannedHostAccess, AHostWriteInvalidatesEveryOtherSpace)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();
        std::vector<std::int32_t> device_bytes = pattern(0);
        ASSERT_EQ(directory_.set_instance(handle, K_DEVICE_0, device_bytes.data()), HandleStatus::Ok);
        directory_.coherence(handle)->record_write(0, K_BYTES, K_DEVICE_0);

        const std::vector<std::int32_t> host_value = pattern(50);
        ASSERT_TRUE(access_.write(handle, 0, host_value.data(), sizeof(std::int32_t)));

        EXPECT_EQ(directory_.coherence(handle)->valid_spaces_at(0), std::uint64_t{1} << K_HOST.value());
        EXPECT_EQ(directory_.coherence(handle)->valid_spaces_at(sizeof(std::int32_t)),
                  std::uint64_t{1} << K_DEVICE_0.value());
        ASSERT_EQ(directory_.drop_instance(handle, K_DEVICE_0), HandleStatus::Ok);
        access_.release(handle);
    }

    TEST_F(Small_PlannedHostAccess, BytesNoSpaceHoldsReadAsZero)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();
        std::vector<std::int32_t> read(K_COUNT, -1);
        ASSERT_TRUE(access_.read(handle, 0, read.data(), K_BYTES));
        EXPECT_EQ(read, std::vector<std::int32_t>(K_COUNT, 0));
        EXPECT_EQ(allocator_.held, 0);
        access_.release(handle);
    }

    TEST_F(Small_PlannedHostAccess, AGraphWithNoPlannedBufferGetsThePlanItGotBefore)
    {
        std::vector<std::int32_t> home(K_COUNT);
        const GlobalHandle placed = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
        ASSERT_EQ(directory_.describe(placed, K_BYTES, ElementLayout{4, 4}, K_DEVICE_0), HandleStatus::Ok);
        ASSERT_EQ(directory_.set_instance(placed, K_DEVICE_0, home.data()), HandleStatus::Ok);
        const GlobalHandle bare = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
        ASSERT_EQ(directory_.describe(bare, K_BYTES, ElementLayout{4, 4}, K_DEVICE_0), HandleStatus::Ok);

        CoherencePlanner subject = planner();
        const Result<CopyPlan> plan = subject.plan({node(K_DEVICE_1, {placed, bare}, {})});
        ASSERT_TRUE(plan);

        PlannedStep allocate;
        allocate.kind = PlannedStepKind::Allocate;
        allocate.handle = placed;
        allocate.space = K_DEVICE_1;
        allocate.end = K_BYTES;
        allocate.alignment = 4;
        allocate.node = 0;
        PlannedStep copy;
        copy.kind = PlannedStepKind::Copy;
        copy.handle = placed;
        copy.space = K_DEVICE_1;
        copy.source = K_DEVICE_0;
        copy.end = K_BYTES;
        copy.node = 0;
        PlannedStep free = allocate;
        free.kind = PlannedStepKind::Free;
        free.alignment = 1;
        free.node = PlannedStep::K_GRAPH_END;
        const std::vector<PlannedStep> expected{allocate, copy, free};
        EXPECT_EQ(plan.value().steps, expected);
    }

    TEST_F(Small_PlannedHostAccess, ReleaseFreesEveryInstanceAndRetiresTheHandle)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();
        const std::vector<std::int32_t> values = pattern(1);
        ASSERT_TRUE(access_.write(handle, 0, values.data(), K_BYTES));
        ASSERT_EQ(directory_.set_instance(handle, K_DEVICE_0, allocator_.allocate(K_DEVICE_0, K_BYTES, 4),
                                          allocator_.kind_of(K_DEVICE_0)),
                  HandleStatus::Ok);
        EXPECT_EQ(allocator_.held, 2);

        access_.release(handle);
        EXPECT_EQ(allocator_.held, 0);
        EXPECT_TRUE(directory_.is_stale(handle));
    }

    TEST_F(Small_PlannedHostAccess, ReleaseFreesThePlannedInstancesOfAHeldBufferAndLeavesItsHeldInstance)
    {
        std::vector<std::int32_t> held_bytes = pattern(3);
        const GlobalHandle handle =
            access_.mint_held(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}, K_DEVICE_1, held_bytes.data()).value();
        ASSERT_EQ(directory_.set_instance(handle, K_DEVICE_0, allocator_.allocate(K_DEVICE_0, K_BYTES, 4),
                                          allocator_.kind_of(K_DEVICE_0)),
                  HandleStatus::Ok);
        const std::vector<MemorySpaceID> held = access_.held_spaces(handle);

        access_.release(handle);

        EXPECT_EQ(held, std::vector<MemorySpaceID>{K_DEVICE_1});
        EXPECT_EQ(allocator_.held, 0);
        EXPECT_EQ(held_bytes, pattern(3));
    }

    TEST_F(Small_PlannedHostAccess, AHostWriteIsRecordedAsAHostWriteOfTheHandle)
    {
        const GlobalHandle handle = access_.mint(HandleKind::Buffer, K_BYTES, ElementLayout{4, 4}).value();
        const std::uint64_t before = directory_.host_write_generation();
        const std::vector<std::int32_t> values = pattern(1);

        ASSERT_TRUE(access_.write(handle, 0, values.data(), K_BYTES));

        EXPECT_GT(directory_.host_write_of(handle), before);
        access_.release(handle);
    }
} // namespace
