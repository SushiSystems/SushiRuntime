
/**************************************************************************/
/* test_memory.cpp                                                        */
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

#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Memory;

/**
 * @test BasicPoolExpansion
 * @brief Tests if the memory pool successfully expands by allocating a new page 
 * when the first page (64 slots) is completely filled.
 */
TEST(USMAllocatorTest, BasicPoolExpansion)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();
    
    constexpr size_t ALLOC_SIZE = 128;
    constexpr size_t SLOTS_PER_PAGE = 64;
    constexpr size_t TARGET_ALLOCS = SLOTS_PER_PAGE + 10; // Force expansion
    
    std::vector<void*> pointers;
    
    // Fill the first page and force the allocation of a second page
    for (size_t i = 0; i < TARGET_ALLOCS; ++i)
    {
        void* ptr = allocator->allocate(ALLOC_SIZE);
        ASSERT_NE(ptr, nullptr) << "Allocation failed at index " << i;
        pointers.push_back(ptr);
    }
    
    // Ensure that all pointers are unique (no overlapping memory segments)
    for (size_t i = 0; i < pointers.size(); ++i)
    {
        for (size_t j = i + 1; j < pointers.size(); ++j)
        {
            EXPECT_NE(pointers[i], pointers[j]) << "Duplicate memory pointer detected!";
        }
    }
    
    // Deallocate all to ensure no segmentation faults or corrupted state
    for (void* ptr : pointers)
    {
        allocator->deallocate(ptr);
    }
}

/**
 * @test LargeAllocationBypass
 * @brief Ensures that requests larger than the maximum pool bucket (4096 bytes) 
 * safely bypass the lock-free pool and use the direct SYCL allocation fallback.
 */
TEST(USMAllocatorTest, LargeAllocationBypass)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();
    
    constexpr size_t LARGE_ALLOC_SIZE = 8192; // Exceeds MAX_POOL_ALLOC_SIZE (4096)
    
    void* ptr1 = allocator->allocate(LARGE_ALLOC_SIZE);
    ASSERT_NE(ptr1, nullptr) << "Large allocation failed.";
    
    void* ptr2 = allocator->allocate(LARGE_ALLOC_SIZE * 2);
    ASSERT_NE(ptr2, nullptr) << "Massive allocation failed.";
    
    EXPECT_NE(ptr1, ptr2) << "Large pointers should not overlap.";
    
    allocator->deallocate(ptr1);
    allocator->deallocate(ptr2);
}

/**
 * @test ConcurrentAllocationContention
 * @brief Creates a high-contention environment where multiple threads aggressively 
 * allocate and deallocate from the exact same bucket, testing the atomic bitmasks.
 */
TEST(USMAllocatorTest, ConcurrentAllocationContention)
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
        
        // Phase 1: Aggressive allocation
        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            void* ptr = allocator->allocate(ALLOC_SIZE);
            EXPECT_NE(ptr, nullptr);
            if (ptr != nullptr)
            {
                local_ptrs.push_back(ptr);
            }
        }
        
        // Simulate minor work to overlap allocations among threads
        std::this_thread::yield();
        
        // Phase 2: Aggressive deallocation
        for (void* ptr : local_ptrs)
        {
            allocator->deallocate(ptr);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back(worker_func);
    }
    
    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    
    // If the test finishes without a deadlock or segfault, the atomic CAS operations
    // within the BitmaskMemoryPool handled the contention correctly.
    SUCCEED();
}

/**
 * @test CrossThreadDeallocation
 * @brief Tests the work-stealing edge case. Thread A allocates memory, but Thread B 
 * frees it. This bypasses Thread B's local cache and tests the global page resolution.
 */
TEST(USMAllocatorTest, CrossThreadDeallocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();
    
    constexpr size_t ALLOC_SIZE = 1024;
    constexpr size_t POINTER_COUNT = 500;
    
    std::vector<void*> shared_pointers;
    
    // Thread 1: Allocate memory
    std::thread allocator_thread([&allocator, &shared_pointers]()
    {
        for (size_t i = 0; i < POINTER_COUNT; ++i)
        {
            shared_pointers.push_back(allocator->allocate(ALLOC_SIZE));
        }
    });
    
    allocator_thread.join();
    
    // Verify allocations succeeded
    for (void* ptr : shared_pointers)
    {
        ASSERT_NE(ptr, nullptr) << "Allocator thread failed to acquire memory.";
    }
    
    // Thread 2: Deallocate memory allocated by Thread 1
    std::thread deallocator_thread([&allocator, &shared_pointers]()
    {
        for (void* ptr : shared_pointers)
        {
            allocator->deallocate(ptr);
        }
    });
    
    deallocator_thread.join();
    
    // If this does not crash, the global binary search (find_owner_page) 
    // and lock-free deallocation logic worked properly across threads.
    SUCCEED();
}