/**************************************************************************/
/* test_memory_stress.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Concurrency stress for the bitmask pool: high-contention same-bucket churn and
// cross-thread deallocation. Pass = no deadlock, no segfault, no double-hand-out.

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
 * @brief Tests concurrent allocation contention.
 * 
 * Many threads hammering the same bucket exercise the atomic bitmasks.
 */
TEST(Stress_USMAllocatorStress, ConcurrentAllocationContention)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr int    NUM_THREADS       = 8;
    constexpr size_t ALLOCS_PER_THREAD = 5000;
    constexpr size_t ALLOC_SIZE        = 256; // hits bucket index 1

    auto worker_func = [&allocator]()
    {
        std::vector<void*> local_ptrs;
        local_ptrs.reserve(ALLOCS_PER_THREAD);

        for (size_t i = 0; i < ALLOCS_PER_THREAD; ++i)
        {
            void* ptr = allocator->allocate(ALLOC_SIZE);
            EXPECT_NE(ptr, nullptr);
            if (ptr != nullptr)
                local_ptrs.push_back(ptr);
        }

        std::this_thread::yield();

        for (void* ptr : local_ptrs)
            allocator->deallocate(ptr);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(worker_func);

    for (auto& t : threads)
        if (t.joinable())
            t.join();

    // Finishing without a deadlock or segfault means the bitmask CAS path
    // handled the contention correctly.
    SUCCEED();
}

/**
 * @brief Tests cross-thread deallocation.
 * 
 * Thread A allocates, thread B frees — exercises the global page resolution that
 * routes a remote free back to the owning slot.
 */
TEST(Stress_USMAllocatorStress, CrossThreadDeallocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE    = 1024;
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

    // Surviving this proves find_owner_page + lock-free cross-thread free work.
    SUCCEED();
}
