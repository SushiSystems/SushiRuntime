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
 * @file test_work_stealing_deque.cpp
 * @brief Tests the Chase-Lev deque: overflow past capacity and no loss under concurrent steals.
 * @see tests/engine/work_stealing/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/task/task.hpp>

#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Scheduler::Deque;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief Tasks pushed past the ring's capacity. */
    constexpr std::uintptr_t K_EXTRA = 5000;

    /** @brief Opaque, non-dereferenced task tag (the deque only moves pointers). */
    Task* tag(std::uintptr_t id) { return reinterpret_cast<Task*>(id); }

    /** @brief Recovers the id that tag() encoded. */
    std::uintptr_t untag(Task* t) { return reinterpret_cast<std::uintptr_t>(t); }

    /** @brief Returns how many tasks fill the ring and the overflow list together. */
    std::uintptr_t total_past_capacity() { return static_cast<std::uintptr_t>(Deque::K_CAPACITY) + K_EXTRA; }

    /** @brief Pushes the tags 1 to @p total onto @p deque, in that order. */
    void push_tags(Deque& deque, std::uintptr_t total)
    {
        for (std::uintptr_t id = 1; id <= total; ++id)
            deque.push(tag(id));
    }

    /** @brief Pops @p deque empty on the calling thread and returns the ids it held, each once. */
    std::set<std::uintptr_t> pop_all(Deque& deque)
    {
        std::set<std::uintptr_t> seen;
        while (Task* task = deque.pop())
            seen.insert(untag(task));
        return seen;
    }

    /** @brief What a pool of consumers took from a deque. */
    struct Taken
    {
        /** @brief How many tasks all consumers took so far. */
        std::atomic<std::uintptr_t> count{0};
    };

    /** @brief Steals from @p deque once @p go is set, until @p taken reaches @p total, recording the ids in @p got. */
    void steal_until_all_taken(Deque& deque, const std::atomic<bool>& go, Taken& taken, std::uintptr_t total,
                               std::vector<std::uintptr_t>& got)
    {
        while (!go.load(std::memory_order_acquire))
            std::this_thread::yield();
        while (taken.count.load(std::memory_order_acquire) < total)
        {
            if (Task* task = deque.steal())
            {
                got.push_back(untag(task));
                taken.count.fetch_add(1, std::memory_order_acq_rel);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    /** @brief Pops from @p deque until @p taken reaches @p total, recording the ids in @p got. */
    void pop_until_all_taken(Deque& deque, Taken& taken, std::uintptr_t total, std::vector<std::uintptr_t>& got)
    {
        while (taken.count.load(std::memory_order_acquire) < total)
        {
            if (Task* task = deque.pop())
            {
                got.push_back(untag(task));
                taken.count.fetch_add(1, std::memory_order_acq_rel);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }
} // namespace

/** @brief Tests that pushing past K_CAPACITY parks the surplus in overflow, never dropping it. */
TEST(Small_WorkStealingDeque, PushBeyondCapacityParksOverflow)
{
    Deque dq;

    push_tags(dq, total_past_capacity());

    EXPECT_EQ(dq.size(), static_cast<size_t>(Deque::K_CAPACITY));
    EXPECT_EQ(dq.overflow_size(), K_EXTRA);
}

/**
 * @brief Tests that draining single-threaded returns every pushed task exactly once.
 *
 * Proves pop() relocates overflowed tasks back into the ring rather than
 * stranding them: the count out must equal the count in, with no duplicates.
 */
TEST(Small_WorkStealingDeque, PopDrainsOverflowExactlyOnce)
{
    Deque dq;
    const std::uintptr_t total = total_past_capacity();
    push_tags(dq, total);

    const std::set<std::uintptr_t> seen = pop_all(dq);

    EXPECT_EQ(dq.size(), 0u);
    EXPECT_EQ(dq.overflow_size(), 0u);
    ASSERT_EQ(seen.size(), total);
    EXPECT_EQ(*seen.begin(), 1u);
    EXPECT_EQ(*seen.rbegin(), total);
}

/**
 * @brief Tests that, under concurrent steals, an overflowing deque still loses no task.
 *
 * One owner pushes past capacity (so the overflow buffer fills) and pops; several
 * stealers drain the top concurrently. Chase-Lev guarantees each task is taken by
 * exactly one consumer, so the union of everything retrieved must be the full set
 * pushed: no loss, no duplication.
 */
TEST(Medium_WorkStealingDeque, NoLossUnderConcurrentSteal)
{
    Deque dq;
    const std::uintptr_t total = total_past_capacity();
    constexpr int NUM_STEALERS = 3;
    Taken taken;
    std::atomic<bool> go{false};
    std::vector<std::uintptr_t> owner_got;
    std::vector<std::vector<std::uintptr_t>> stealer_got(NUM_STEALERS);
    std::vector<std::thread> stealers;
    for (std::vector<std::uintptr_t>& got : stealer_got)
        stealers.emplace_back(steal_until_all_taken, std::ref(dq), std::cref(go), std::ref(taken), total,
                              std::ref(got));

    // Owner thread = this thread: sole pusher and popper (Chase-Lev invariant).
    push_tags(dq, total);
    go.store(true, std::memory_order_release);
    pop_until_all_taken(dq, taken, total, owner_got);
    for (std::thread& stealer : stealers)
        stealer.join();

    std::vector<std::uintptr_t> everything = owner_got;
    for (const std::vector<std::uintptr_t>& got : stealer_got)
        everything.insert(everything.end(), got.begin(), got.end());
    const std::set<std::uintptr_t> distinct(everything.begin(), everything.end());
    EXPECT_EQ(everything.size(), total) << "every task retrieved exactly once (no duplicates)";
    EXPECT_EQ(distinct.size(), total) << "every distinct task retrieved (no loss)";
}
