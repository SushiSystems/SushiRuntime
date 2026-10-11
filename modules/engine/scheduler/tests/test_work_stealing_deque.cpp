/**************************************************************************/
/* test_work_stealing_deque.cpp                                           */
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
 * @file test_work_stealing_deque.cpp
 * @brief Tests that the Chase-Lev deque returns every pushed task exactly once past its capacity.
 * @see modules/engine/scheduler/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "work_stealing_deque.hpp"

using SushiRuntime::Internal::Scheduler::Deque;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief Opaque, non-dereferenced task tag (the deque only moves pointers). */
    Task* tag(std::uintptr_t id) { return reinterpret_cast<Task*>(id); }
    std::uintptr_t untag(Task* t) { return reinterpret_cast<std::uintptr_t>(t); }
}

/**
 * @brief Pushing past K_CAPACITY parks the surplus in overflow, never dropping it.
 */
TEST(Unit_WorkStealingDeque, PushBeyondCapacityParksOverflow)
{
    Deque dq;
    const std::uintptr_t extra = 5000;
    const std::uintptr_t total = static_cast<std::uintptr_t>(Deque::K_CAPACITY) + extra;

    for (std::uintptr_t i = 1; i <= total; ++i)
        dq.push(tag(i));

    EXPECT_EQ(dq.size(), static_cast<size_t>(Deque::K_CAPACITY));
    EXPECT_EQ(dq.overflow_size(), extra);
}

/**
 * @brief Draining single-threaded returns every pushed task exactly once.
 *
 * Proves pop() relocates overflowed tasks back into the ring rather than
 * stranding them: the count out must equal the count in, with no duplicates.
 */
TEST(Unit_WorkStealingDeque, PopDrainsOverflowExactlyOnce)
{
    Deque dq;
    const std::uintptr_t total = static_cast<std::uintptr_t>(Deque::K_CAPACITY) + 5000;

    for (std::uintptr_t i = 1; i <= total; ++i)
        dq.push(tag(i));

    std::set<std::uintptr_t> seen;
    while (Task* t = dq.pop())
        seen.insert(untag(t));

    EXPECT_EQ(dq.size(), 0u);
    EXPECT_EQ(dq.overflow_size(), 0u);
    ASSERT_EQ(seen.size(), total);
    EXPECT_EQ(*seen.begin(), 1u);
    EXPECT_EQ(*seen.rbegin(), total);
}

/**
 * @brief Under concurrent steals, an overflowing deque still loses no task.
 *
 * One owner pushes past capacity (so the overflow buffer fills) and pops; several
 * stealers drain the top concurrently. Chase-Lev guarantees each task is taken by
 * exactly one consumer, so the union of everything retrieved must be the full set
 * pushed — no loss, no duplication.
 */
TEST(Unit_WorkStealingDeque, NoLossUnderConcurrentSteal)
{
    Deque dq;
    const std::uintptr_t total = static_cast<std::uintptr_t>(Deque::K_CAPACITY) + 5000;
    constexpr int NUM_STEALERS = 3;

    std::atomic<std::uintptr_t> retrieved{0};
    std::vector<std::uintptr_t> owner_got;
    std::vector<std::vector<std::uintptr_t>> stealer_got(NUM_STEALERS);

    std::atomic<bool> go{false};

    std::vector<std::thread> stealers;
    for (int k = 0; k < NUM_STEALERS; ++k)
    {
        stealers.emplace_back([&, k]
        {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            while (retrieved.load(std::memory_order_acquire) < total)
            {
                if (Task* t = dq.steal())
                {
                    stealer_got[k].push_back(untag(t));
                    retrieved.fetch_add(1, std::memory_order_acq_rel);
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Owner thread = this thread: sole pusher and popper (Chase-Lev invariant).
    for (std::uintptr_t i = 1; i <= total; ++i)
        dq.push(tag(i));
    go.store(true, std::memory_order_release);

    while (retrieved.load(std::memory_order_acquire) < total)
    {
        if (Task* t = dq.pop())
        {
            owner_got.push_back(untag(t));
            retrieved.fetch_add(1, std::memory_order_acq_rel);
        }
        else
        {
            std::this_thread::yield();
        }
    }

    for (auto& s : stealers) s.join();

    std::set<std::uintptr_t> seen;
    size_t count = owner_got.size();
    for (auto id : owner_got) seen.insert(id);
    for (auto& vec : stealer_got)
    {
        count += vec.size();
        for (auto id : vec) seen.insert(id);
    }

    EXPECT_EQ(count, total) << "every task retrieved exactly once (no duplicates)";
    EXPECT_EQ(seen.size(), total) << "every distinct task retrieved (no loss)";
}
