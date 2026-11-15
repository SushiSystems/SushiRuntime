/**************************************************************************/
/* test_usm_allocation_owner.cpp                                          */
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
 * @file test_usm_allocation_owner.cpp
 * @brief Tests USMAllocationOwner: an Allocation returns its USM to the allocator once.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>

#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

using namespace SushiRuntime::Internal::Memory;
using SushiRuntime::Detail::Allocation;

namespace
{
    /** @brief Number of devices the recording source holds. */
    constexpr std::size_t K_DEVICES = 2;

    /** @brief Hands out one fixed block and records each free with its device. */
    class RecordingSource final : public USMSource
    {
        public:
            /** @brief Takes the list each free is appended to; it must outlive the source. */
            explicit RecordingSource(std::vector<std::pair<void*, std::size_t>>& frees) : frees_(frees) {}

            [[nodiscard]] std::size_t device_count() const noexcept override { return K_DEVICES; }

            [[nodiscard]] void* allocate(const USMRequest&) override { return block_; }

            void free(void* address, std::size_t device_index) override
            {
                frees_.emplace_back(address, device_index);
            }

            [[nodiscard]] bool shares_context(std::size_t, std::size_t) const override { return true; }

            void copy(void*, const void*, std::size_t, std::size_t) override {}

            [[nodiscard]] std::string device_name(std::size_t) const override { return "recording"; }

        private:
            /// @brief The block every allocation returns.
            unsigned char block_[64] = {};

            /// @brief Each free heard, with its device.
            std::vector<std::pair<void*, std::size_t>>& frees_;
    };
} // namespace

/** @brief Tests that destroying the allocation frees the USM once, on the owner's device. */
TEST(Small_USMAllocationOwner, DestroyingTheAllocationFreesTheUSMOnItsDevice)
{
    constexpr std::size_t K_BYTES = 64;
    std::vector<std::pair<void*, std::size_t>> frees;
    auto allocator = std::make_shared<USMAllocator>(std::make_unique<RecordingSource>(frees));
    USMRequest request;
    request.bytes = K_BYTES;
    request.device_index = K_DEVICES - 1;
    void* const address = allocator->allocate(request);
    ASSERT_NE(address, nullptr);

    {
        Allocation allocation(address, K_BYTES,
                              std::make_unique<USMAllocationOwner>(allocator, K_DEVICES - 1));
        EXPECT_TRUE(frees.empty());
    }
    const std::vector<std::pair<void*, std::size_t>> expected{{address, K_DEVICES - 1}};
    EXPECT_EQ(frees, expected);
}

TEST(Small_USMAllocationOwner, AnOwnerWithNoAllocatorLeavesTheMemoryAlone)
{
    constexpr int K_KEPT = 7;
    USMAllocationOwner owner(nullptr, 0);
    int value = K_KEPT;

    owner.release(&value);

    EXPECT_EQ(value, K_KEPT);
}
