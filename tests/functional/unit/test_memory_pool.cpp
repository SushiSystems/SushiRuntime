/**************************************************************************/
/* test_memory_pool.cpp                                                   */
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
 * @file test_memory_pool.cpp
 * @brief Unit coverage for the USM allocator / bitmask pool.
 *
 * Mostly single-threaded, deterministic invariants: page expansion, large-
 * allocation bypass, size-class boundaries, degenerate sizes. The heavy,
 * high-contention churn lives in the benchmark/stress suite, but one
 * deterministic cross-thread free/reclaim test lives here too: it is the
 * correctness guard for the lock-free remote-free + lazy-drain path, and that
 * path is too important to be validated only in the nightly benchmark suite.
 */

#include <vector>
#include <thread>
#include <barrier>
#include <unordered_set>
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
TEST(Unit_USMAllocatorTest, BasicPoolExpansion)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE = 128;
    constexpr size_t SLOTS_PER_PAGE = 64;
    constexpr size_t TARGET_ALLOCS = SLOTS_PER_PAGE + 10; // Force expansion

    std::vector<void*> pointers;

    for (size_t i = 0; i < TARGET_ALLOCS; ++i)
    {
        void* ptr = allocator->allocate(ALLOC_SIZE);
        ASSERT_NE(ptr, nullptr) << "Allocation failed at index " << i;
        pointers.push_back(ptr);
    }

    // Ensure that all pointers are unique (no overlapping memory segments)
    for (size_t i = 0; i < pointers.size(); ++i)
        for (size_t j = i + 1; j < pointers.size(); ++j)
            EXPECT_NE(pointers[i], pointers[j]) << "Duplicate memory pointer detected!";

    for (void* ptr : pointers)
        allocator->deallocate(ptr);
}

/**
 * @test LargeAllocationBypass
 * @brief Ensures that requests larger than the maximum pool bucket (4096 bytes)
 * safely bypass the lock-free pool and use the direct SYCL allocation fallback.
 */
TEST(Unit_USMAllocatorTest, LargeAllocationBypass)
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
 * @test SizeClassBoundaries
 * @brief Walks every pooled size class boundary (64, 256, 1024, 4096) plus the
 * first size just above the pool ceiling. Every class must return a usable,
 * writable, distinct allocation.
 */
TEST(Unit_USMAllocatorTest, SizeClassBoundaries)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    // Boundary sizes: exact class sizes, one below, one above the ceiling.
    for (size_t sz : {1u, 63u, 64u, 65u, 256u, 1024u, 4096u, 4097u})
    {
        void* p = allocator->allocate(sz);
        ASSERT_NE(p, nullptr) << "Allocation failed for size " << sz;

        // Shared USM is host-writable; touch first and last byte to confirm.
        auto* bytes = static_cast<volatile char*>(p);
        bytes[0] = 1;
        bytes[sz - 1] = 1;

        allocator->deallocate(p);
    }
}

/**
 * @test ZeroByteAllocation
 * @brief A zero-byte request must not crash. Whatever the policy (null or a
 * valid minimal pointer), deallocation of the result must be safe.
 */
TEST(Unit_USMAllocatorTest, ZeroByteAllocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    void* p = nullptr;
    EXPECT_NO_THROW({ p = allocator->allocate(0); });

    // Freeing whatever came back (including nullptr) must be safe.
    EXPECT_NO_THROW({ allocator->deallocate(p); });
}

/**
 * @test NullDeallocation
 * @brief Deallocating a null pointer is a documented no-op and must never fault.
 */
TEST(Unit_USMAllocatorTest, NullDeallocation)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    EXPECT_NO_THROW({ allocator->deallocate(nullptr); });
}

/**
 * @test ReusedSlotAfterFree
 * @brief After freeing a slot, the very next same-size allocation should be
 * serviceable (pool recycles rather than growing unboundedly). We allocate,
 * free, and re-allocate in a tight loop and assert every result is valid.
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
 * @test CrossThreadFreeIsReclaimed
 * @brief Deterministic guard for the lock-free remote-free + lazy-drain path.
 *
 * A producer thread (which owns the pages, i.e. the pool slot) allocates a wave
 * of distinct blocks. A *different* thread frees them all -- these are remote
 * frees, pushed onto the owning slot's intrusive stack. The producer then
 * allocates a second wave on the same slot, which must lazily drain that stack
 * and hand the freed blocks back.
 *
 * Two invariants a broken drain would violate:
 *   1. No live aliasing: within each wave every pointer is distinct (the pool
 *      never hands out a block that is still in use).
 *   2. Reclamation, not growth: the second wave reuses cross-thread-freed
 *      blocks rather than only minting fresh ones. A broken drain would leak
 *      the freed memory onto the stack and wave 2 would reuse none of it.
 *
 * Re-allocation must run on the *same* thread that owns the pages, because the
 * owning slot is the only one that drains its remote-free stack -- hence the
 * barriers rather than two short-lived threads.
 */
TEST(Unit_USMAllocatorTest, CrossThreadFreeIsReclaimed)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    constexpr size_t ALLOC_SIZE = 256;
    constexpr size_t N = 4096;   // > magazine BATCH_SIZE so the drain path runs

    std::vector<void*> wave1(N, nullptr);
    std::vector<void*> wave2(N, nullptr);

    std::barrier allocated(2);   // wave 1 done -> consumer may free
    std::barrier freed(2);       // consumer done -> producer may re-allocate

    std::thread producer([&] {
        for (size_t i = 0; i < N; ++i) wave1[i] = allocator->allocate(ALLOC_SIZE);
        allocated.arrive_and_wait();
        freed.arrive_and_wait();
        // Same slot as wave 1: this is where the remote-free stack gets drained.
        for (size_t i = 0; i < N; ++i) wave2[i] = allocator->allocate(ALLOC_SIZE);
    });

    std::thread consumer([&] {
        allocated.arrive_and_wait();
        for (size_t i = 0; i < N; ++i) allocator->deallocate(wave1[i]);  // remote free
        freed.arrive_and_wait();
    });

    producer.join();
    consumer.join();

    // (1) No live aliasing within either wave.
    for (void* p : wave1) ASSERT_NE(p, nullptr);
    std::unordered_set<void*> s1(wave1.begin(), wave1.end());
    ASSERT_EQ(s1.size(), N) << "Wave 1 returned aliased live pointers";

    std::unordered_set<void*> s2;
    for (void* p : wave2) {
        ASSERT_NE(p, nullptr);
        ASSERT_TRUE(s2.insert(p).second) << "Wave 2 returned aliased live pointers";
    }

    // (2) Reclamation: wave 2 must reuse cross-thread-freed blocks.
    size_t reused = 0;
    for (void* p : wave2) if (s1.count(p)) ++reused;
    EXPECT_GT(reused, 0u)
        << "No cross-thread-freed block was reclaimed -- remote-free drain is not working";

    for (void* p : wave2) allocator->deallocate(p);
}
