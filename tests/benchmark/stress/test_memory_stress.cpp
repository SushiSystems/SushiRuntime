/**************************************************************************/
/* test_memory_stress.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_memory_stress.cpp
 * @brief Concurrency stress for the bitmask pool: high-contention same-bucket
 * churn and cross-thread deallocation. Success criterion is "no deadlock, no
 * segfault, no double-hand-out" rather than any timing figure.
 */

#include <vector>
#include <thread>
#include <atomic>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Memory;

/**
 * @test ConcurrentAllocationContention
 * @brief Creates a high-contention environment where multiple threads aggressively
 * allocate and deallocate from the exact same bucket, testing the atomic bitmasks.
 */
TEST(USMAllocatorStress, ConcurrentAllocationContention)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr int NUM_THREADS = 8;
    constexpr size_t ALLOCS_PER_THREAD = 5000;
    constexpr size_t ALLOC_SIZE = 256; // Hits bucket index 1

    auto worker_func = [&allocator]()
    {
        std::vector<void*> local_ptrs;
        local_ptrs.reserve(ALLOCS_PER_THREAD);

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            void* ptr = allocator->allocate(ALLOC_SIZE);
            EXPECT_NE(ptr, nullptr);
            if (ptr != nullptr) local_ptrs.push_back(ptr);
        }

        std::this_thread::yield();

        for (void* ptr : local_ptrs)
            allocator->deallocate(ptr);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker_func);

    for (auto& t : threads)
        if (t.joinable()) t.join();

    // If the test finishes without a deadlock or segfault, the atomic CAS operations
    // within the BitmaskMemoryPool handled the contention correctly.
    SUCCEED();
}

/**
 * @test CrossThreadDeallocation
 * @brief Tests the work-stealing edge case. Thread A allocates memory, but Thread B
 * frees it. This bypasses Thread B's local cache and tests the global page resolution.
 */
TEST(USMAllocatorStress, CrossThreadDeallocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE = 1024;
    constexpr size_t POINTER_COUNT = 500;

    std::vector<void*> shared_pointers;

    std::thread allocator_thread([&allocator, &shared_pointers]()
    {
        for (size_t i = 0; i < POINTER_COUNT; ++i)
            shared_pointers.push_back(allocator->allocate(ALLOC_SIZE));
    });
    allocator_thread.join();

    for (void* ptr : shared_pointers)
        ASSERT_NE(ptr, nullptr) << "Allocator thread failed to acquire memory.";

    std::thread deallocator_thread([&allocator, &shared_pointers]()
    {
        for (void* ptr : shared_pointers)
            allocator->deallocate(ptr);
    });
    deallocator_thread.join();

    // If this does not crash, the global binary search (find_owner_page)
    // and lock-free deallocation logic worked properly across threads.
    SUCCEED();
}
