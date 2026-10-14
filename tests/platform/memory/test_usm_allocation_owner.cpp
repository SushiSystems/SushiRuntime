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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_usm_allocation_owner.cpp
 * @brief Tests USMAllocationOwner: an Allocation returns its USM to the allocator once.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/memory/allocation.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime::Internal::Memory;

/** @brief Tests that destroying the allocation frees the USM through the allocator. */
TEST(Unit_USMAllocationOwner, DestroyingTheAllocationFreesTheUSM)
{
    if (sycl::device::get_devices().empty())
        GTEST_SKIP() << "no SYCL device";
    auto allocator = std::make_shared<USMAllocator>(
        std::vector<sycl::queue>{sycl::queue(sycl::device::get_devices().front())});
    USMRequest request;
    request.bytes = 128;
    void* const address = allocator->allocate(request);
    if (address == nullptr)
        GTEST_SKIP() << "the device gives no shared memory";
    const std::size_t live = allocator->live_count();

    {
        Allocation allocation(address, 128, std::make_unique<USMAllocationOwner>(allocator));
        EXPECT_EQ(allocator->live_count(), live);
    }
    EXPECT_EQ(allocator->live_count(), live - 1);
}

/** @brief Tests that an owner with no allocator releases nothing and does not stop. */
TEST(Unit_USMAllocationOwner, AnOwnerWithNoAllocatorDoesNothing)
{
    USMAllocationOwner owner(nullptr);
    int value = 0;
    owner.release(&value);
    SUCCEED();
}
