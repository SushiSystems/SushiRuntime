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

#include <gtest/gtest.h>

#include <SushiRuntime/core/allocation.hpp>

#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime::Internal::Memory;
using SushiRuntime::Detail::Allocation;

/** @brief Tests that destroying the allocation frees the USM through the allocator. */
TEST(Small_USMAllocationOwner, DestroyingTheAllocationFreesTheUSM)
{
    constexpr std::size_t K_BYTES = 128;
    auto allocator = std::make_shared<USMAllocator>(make_host_usm_source());
    USMRequest request;
    request.bytes = K_BYTES;
    void* const address = allocator->allocate(request);
    ASSERT_NE(address, nullptr);
    const std::size_t live = allocator->live_count();

    {
        Allocation allocation(address, K_BYTES, std::make_unique<USMAllocationOwner>(allocator));
        EXPECT_EQ(allocator->live_count(), live);
    }
    EXPECT_EQ(allocator->live_count(), live - 1);
}

TEST(Small_USMAllocationOwner, AnOwnerWithNoAllocatorLeavesTheMemoryAlone)
{
    constexpr int K_KEPT = 7;
    USMAllocationOwner owner(nullptr);
    int value = K_KEPT;

    owner.release(&value);

    EXPECT_EQ(value, K_KEPT);
}
