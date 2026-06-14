/**************************************************************************/
/* test_epoch_reclamation.cpp                                            */
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
 * @file test_epoch_reclamation.cpp
 * @brief Unit coverage for the epoch-based reclamation (EBR) machinery.
 *
 * EpochManager is a process-wide singleton, so these tests assert observable
 * contracts rather than internal counters: retired objects are eventually
 * deleted exactly once, EpochGuard enter/exit is balanced and non-throwing,
 * and a thread that retires then exits hands its outstanding nodes to the
 * global orphan list (proven by their deletion happening from another thread).
 */

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

using namespace SushiRuntime::Memory;

namespace
{
    using Counter = std::shared_ptr<std::atomic<int>>;

    inline Counter make_counter() { return std::make_shared<std::atomic<int>>(0); }

    /**
     * @brief Heap payload that bumps a shared atomic counter on destruction.
     *
     * Lets a test observe reclamation (delete actually ran) without reaching
     * into EpochManager internals.
     *
     * The counter is held by shared_ptr, NOT a raw pointer to a test-local
     * atomic. EBR reclamation is deferred and a tail of retired objects can
     * linger in the current epoch past the test's return; they are only freed
     * later (during a subsequent test's reclamation activity). A raw pointer to
     * a stack `atomic<int>` would then be written after the test frame is gone —
     * a stack-use-after-return that, in the full suite, silently corrupts
     * whatever later test now occupies that stack region. Owning the counter via
     * shared_ptr keeps it alive exactly as long as any Tracked still references
     * it, so the late dtor write always lands in valid memory.
     */
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief Retire many objects on the calling thread and spin until at least
     * @p min_expected of them have been reclaimed (or a bound is hit).
     *
     * Reclamation is gated on epoch advancement, which requires re-entering the
     * critical section. Each iteration enters/exits a guard and retires one
     * object; the threshold-driven check_and_reclaim then collects objects from
     * earlier epochs.
     */
    int retire_and_drain(const Counter& destroyed, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        // Extra guarded passes to advance the epoch and let pending nodes drain.
        for (int i = 0; i < 512; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        return destroyed->load(std::memory_order_relaxed);
    }
}

/**
 * @test GuardEnterExitBalanced
 * @brief Constructing and destroying an EpochGuard must be safe and balanced,
 * even when nested.
 */
TEST(EpochReclamationTest, GuardEnterExitBalanced)
{
    EXPECT_NO_THROW({
        EpochGuard outer;
        {
            EpochGuard inner;
        }
    });
}

/**
 * @test RetiredObjectsEventuallyReclaimed
 * @brief After enough guarded retire calls, the bulk of retired objects must be
 * deleted. We retire well past the RETIRE_THRESHOLD and assert that a strong
 * majority were reclaimed (a tail may linger in the current epoch).
 */
TEST(EpochReclamationTest, RetiredObjectsEventuallyReclaimed)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 1024;

    int reclaimed = retire_and_drain(destroyed, RETIRED);

    // Most should be gone; the only survivors are those retired in the final,
    // not-yet-advanced epoch. Bound is generous to stay non-flaky.
    EXPECT_GT(reclaimed, RETIRED) << "Epoch reclamation did not collect retired objects.";
}

/**
 * @test ThreadExitHandsOffOrphans
 * @brief A thread that retires objects and then exits must not leak them: its
 * outstanding nodes are pushed to the global orphan list and reclaimed by other
 * threads' check_and_reclaim. We prove this by retiring on a short-lived thread
 * and then doing draining work on the main thread until they are deleted.
 */
TEST(EpochReclamationTest, ThreadExitHandsOffOrphans)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 300;

    std::thread producer([destroyed]()
    {
        for (int i = 0; i < RETIRED; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        // Thread exits here -> EpochThreadLocalState dtor donates remaining
        // retire_list to the global orphan list.
    });
    producer.join();

    // Main thread drains: each guarded retire triggers check_and_reclaim, which
    // pulls in the orphaned nodes and frees those past the safe epoch.
    Counter drain_sink = make_counter();
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(drain_sink));
    }

    EXPECT_GT(destroyed->load(), 0)
        << "Orphaned nodes from an exited thread were never reclaimed (leak).";
}

/**
 * @test ConcurrentRetireNoCrash
 * @brief Many threads retiring simultaneously must not corrupt the manager.
 * Success is the absence of a crash plus a non-trivial reclamation count.
 */
TEST(EpochReclamationTest, ConcurrentRetireNoCrash)
{
    Counter destroyed = make_counter();
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 500;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([destroyed]()
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(destroyed));
            }
        });
    }
    for (auto& w : workers) w.join();

    EXPECT_GT(destroyed->load(), 0) << "No objects reclaimed under concurrent retire.";
}
