/**************************************************************************/
/* test_memory_pool.cpp                                                   */
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

// Unit coverage for the USM allocator / bitmask pool: page expansion, large-
// allocation bypass, size-class boundaries, degenerate sizes. Heavy churn lives
// in benchmark/stress, but the lock-free remote-free + lazy-drain path gets one
// deterministic guard here too.

#include <vector>
#include <thread>
#include <unordered_set>
#include <gtest/gtest.h>
#include "test_barrier.hpp"
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Memory;

/**
 * @brief Tests that filling the first page grows the pool to a new page.
 *
 * Allocates beyond the initial page slots to force an expansion.
 */
TEST(Unit_USMAllocatorTest, BasicPoolExpansion)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE     = 128;
    constexpr size_t SLOTS_PER_PAGE = 64;
    constexpr size_t TARGET_ALLOCS  = SLOTS_PER_PAGE + 10; // force expansion

    std::vector<void*> pointers;

    for (size_t i = 0; i < TARGET_ALLOCS; ++i)
    {
        void* ptr = allocator->allocate(ALLOC_SIZE);
        ASSERT_NE(ptr, nullptr) << "Allocation failed at index " << i;
        pointers.push_back(ptr);
    }

    // Every pointer must be unique (no overlapping segments).
    for (size_t i = 0; i < pointers.size(); ++i)
        for (size_t j = i + 1; j < pointers.size(); ++j)
            EXPECT_NE(pointers[i], pointers[j]) << "Duplicate memory pointer detected!";

    for (void* ptr : pointers)
        allocator->deallocate(ptr);
}

/**
 * @brief Tests that requests above the largest pool bucket bypass the pool.
 *
 * Large allocations go directly to SYCL and return non-overlapping pointers.
 */
TEST(Unit_USMAllocatorTest, LargeAllocationBypass)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t LARGE_ALLOC_SIZE = 8192; // exceeds MAX_POOL_ALLOC_SIZE (4096)

    void* ptr1 = allocator->allocate(LARGE_ALLOC_SIZE);
    ASSERT_NE(ptr1, nullptr) << "Large allocation failed.";

    void* ptr2 = allocator->allocate(LARGE_ALLOC_SIZE * 2);
    ASSERT_NE(ptr2, nullptr) << "Massive allocation failed.";

    EXPECT_NE(ptr1, ptr2) << "Large pointers should not overlap.";

    allocator->deallocate(ptr1);
    allocator->deallocate(ptr2);
}

/**
 * @brief Tests that every pooled size class returns a distinct, usable allocation.
 *
 * Checks allocations at boundaries including the first size past the ceiling.
 */
TEST(Unit_USMAllocatorTest, SizeClassBoundaries)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    for (size_t sz : {1u, 63u, 64u, 65u, 256u, 1024u, 4096u, 4097u})
    {
        void* p = allocator->allocate(sz);
        ASSERT_NE(p, nullptr) << "Allocation failed for size " << sz;

        // Shared USM is host-writable; touch first and last byte to confirm.
        auto* bytes = static_cast<volatile char*>(p);
        bytes[0]      = 1;
        bytes[sz - 1] = 1;

        allocator->deallocate(p);
    }
}

/**
 * @brief Tests that a zero-byte request does not crash and freeing it is safe.
 *
 * Verifies edge-case behavior of 0-byte allocations.
 */
TEST(Unit_USMAllocatorTest, ZeroByteAllocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    void* p = nullptr;
    EXPECT_NO_THROW({ p = allocator->allocate(0); });
    EXPECT_NO_THROW({ allocator->deallocate(p); });
}

/**
 * @brief Tests that deallocating a null pointer is a safe no-op.
 *
 * Ensures the allocator gracefully ignores nullptr deallocations.
 */
TEST(Unit_USMAllocatorTest, NullDeallocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    EXPECT_NO_THROW({ allocator->deallocate(nullptr); });
}

/**
 * @brief Tests that freeing then re-allocating the same size recycles memory.
 *
 * Ensures the pool reuses slots instead of growing unboundedly.
 */
TEST(Unit_USMAllocatorTest, ReusedSlotAfterFree)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE = 256;
    for (int i = 0; i < 1000; ++i)
    {
        void* p = allocator->allocate(ALLOC_SIZE);
        ASSERT_NE(p, nullptr) << "Recycled allocation failed at iteration " << i;
        allocator->deallocate(p);
    }
}

/**
 * @brief Tests the lock-free remote-free and lazy-drain path.
 *
 * Verifies that when a different thread frees memory, the owning thread
 * lazily drains its stack and reuses the blocks.
 */
TEST(Unit_USMAllocatorTest, CrossThreadFreeIsReclaimed)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE = 256;
    constexpr size_t N = 4096;   // > magazine BATCH_SIZE so the drain path runs

    std::vector<void*> wave1(N, nullptr);
    std::vector<void*> wave2(N, nullptr);

    SushiRuntime::Test::Barrier allocated(2);   // wave 1 done -> consumer may free
    SushiRuntime::Test::Barrier freed(2);       // consumer done -> producer may re-allocate

    std::thread producer([&]
    {
        for (size_t i = 0; i < N; ++i)
            wave1[i] = allocator->allocate(ALLOC_SIZE);
        allocated.arrive_and_wait();
        freed.arrive_and_wait();
        // Same slot as wave 1: where the remote-free stack gets drained.
        for (size_t i = 0; i < N; ++i)
            wave2[i] = allocator->allocate(ALLOC_SIZE);
    });

    std::thread consumer([&]
    {
        allocated.arrive_and_wait();
        for (size_t i = 0; i < N; ++i)
            allocator->deallocate(wave1[i]);  // remote free
        freed.arrive_and_wait();
    });

    producer.join();
    consumer.join();

    // (1) No live aliasing within either wave.
    for (void* p : wave1)
        ASSERT_NE(p, nullptr);
    std::unordered_set<void*> s1(wave1.begin(), wave1.end());
    ASSERT_EQ(s1.size(), N) << "Wave 1 returned aliased live pointers";

    std::unordered_set<void*> s2;
    for (void* p : wave2)
    {
        ASSERT_NE(p, nullptr);
        ASSERT_TRUE(s2.insert(p).second) << "Wave 2 returned aliased live pointers";
    }

    // (2) Reclamation: wave 2 must reuse cross-thread-freed blocks.
    size_t reused = 0;
    for (void* p : wave2)
        if (s1.count(p))
            ++reused;
    EXPECT_GT(reused, 0u)
        << "No cross-thread-freed block was reclaimed -- remote-free drain is not working";

    for (void* p : wave2)
        allocator->deallocate(p);
}
