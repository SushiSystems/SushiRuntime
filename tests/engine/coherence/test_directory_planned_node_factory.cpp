/**************************************************************************/
/* test_directory_planned_node_factory.cpp                                */
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
 * @file test_directory_planned_node_factory.cpp
 * @brief Tests the shape and the work of the nodes DirectoryPlannedNodeFactory builds.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "fake_transfer_path.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Memory::ElementLayout;
using SushiRuntime::Internal::Memory::ResidencyDirectory;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    constexpr std::size_t K_BYTES = 8;

    /** @brief Hands out host memory and counts what is still held. */
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

            [[nodiscard]] std::optional<Internal::Memory::USMKind> kind_of(MemorySpaceID) const noexcept override
            {
                return std::nullopt;
            }

            int held = 0; /**< Allocations not yet released. */
    };

    /** @brief Holds a buffer on device 0, the factory and a step of each kind on device 1. */
    class Small_DirectoryPlannedNodeFactory : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                buffer_ = directory_.mint(HandleKind::Buffer, HandleFlags::None).handle;
                ASSERT_EQ(directory_.describe(buffer_, K_BYTES, ElementLayout{}, K_DEVICE_0), HandleStatus::Ok);
                ASSERT_EQ(directory_.set_instance(buffer_, K_DEVICE_0, &bytes_), HandleStatus::Ok);
            }

            /** @brief Returns a step of kind on device 1 over the whole buffer. */
            PlannedStep step(PlannedStepKind kind) const
            {
                PlannedStep result;
                result.kind = kind;
                result.handle = buffer_;
                result.space = K_DEVICE_1;
                result.source = K_DEVICE_0;
                result.end = K_BYTES;
                return result;
            }

            /** @brief Returns a queue for host_work to take, or nothing when SYCL offers none. */
            static std::optional<sycl::queue> any_queue()
            {
                try
                {
                    return sycl::queue{};
                }
                catch (const sycl::exception&)
                {
                    return std::nullopt;
                }
            }

            ResidencyDirectory directory_{0};
            std::shared_ptr<HeapAllocator> allocator_ = std::make_shared<HeapAllocator>();
            FakeTransferPath path_;
            DirectoryPlannedNodeFactory factory_{directory_, allocator_, {&path_}};
            GlobalHandle buffer_;
            unsigned char bytes_[K_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8};
    };

    TEST_F(Small_DirectoryPlannedNodeFactory, ANodeRunsOnTheDeviceOfItsSpaceAndDeclaresItsRange)
    {
        const Result<sushi_ptr<Node>> copy = factory_.make(step(PlannedStepKind::Copy));
        ASSERT_TRUE(copy);
        EXPECT_EQ(copy.value()->cached_queue_index_, 1);
        EXPECT_TRUE(copy.value()->locality_checked_);
        EXPECT_EQ(copy.value()->mode, Node::ExecutionMode::Host);
        ASSERT_EQ(copy.value()->io_reads_.size(), 1U);
        ASSERT_EQ(copy.value()->io_writes_.size(), 1U);
        EXPECT_EQ(copy.value()->io_writes_[0].length, K_BYTES);
    }

    TEST_F(Small_DirectoryPlannedNodeFactory, ACopyNamingAPathTheFactoryLacksIsRefused)
    {
        PlannedStep unknown = step(PlannedStepKind::Copy);
        unknown.path = 1;
        const Result<sushi_ptr<Node>> copy = factory_.make(unknown);
        ASSERT_FALSE(copy);
        EXPECT_EQ(copy.error().code, ErrorCode::InvalidArgument);
    }

    TEST_F(Small_DirectoryPlannedNodeFactory, AllocateCopyAndFreeMoveTheBytesAndReleaseTheInstance)
    {
        std::optional<sycl::queue> queue = any_queue();
        if (!queue)
            GTEST_SKIP() << "SYCL offers no queue for host_work to take.";

        const Result<sushi_ptr<Node>> allocate = factory_.make(step(PlannedStepKind::Allocate));
        const Result<sushi_ptr<Node>> copy = factory_.make(step(PlannedStepKind::Copy));
        const Result<sushi_ptr<Node>> free = factory_.make(step(PlannedStepKind::Free));
        ASSERT_TRUE(allocate && copy && free);

        static_cast<void>(allocate.value()->host_work(*queue, {}));
        const ResolveOutcome made = directory_.resolve(buffer_, K_DEVICE_1);
        ASSERT_EQ(made.status, HandleStatus::Ok);

        static_cast<void>(copy.value()->host_work(*queue, {}));
        EXPECT_EQ(path_.copies(), 1);
        EXPECT_EQ(static_cast<unsigned char*>(made.address)[7], 8);

        static_cast<void>(free.value()->host_work(*queue, {}));
        EXPECT_EQ(directory_.resolve(buffer_, K_DEVICE_1).status, HandleStatus::NotResident);
        EXPECT_EQ(allocator_->held, 0);
    }
} // namespace
