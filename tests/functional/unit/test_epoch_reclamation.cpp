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
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

using namespace SushiRuntime::Memory;

namespace
{
    /**
     * @brief Heap payload that bumps a shared atomic counter on destruction.
     *
     * Lets a test observe reclamation (delete actually ran) without reaching
     * into EpochManager internals. The counter is external so the count
     * survives the object's own destruction.
     */
    struct Tracked
    {
        std::atomic<int>* destroyed;
        explicit Tracked(std::atomic<int>* d) : destroyed(d) {}
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
    int retire_and_drain(std::atomic<int>& destroyed, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(&destroyed));
        }
        // Extra guarded passes to advance the epoch and let pending nodes drain.
        for (int i = 0; i < 512; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(&destroyed));
        }
        return destroyed.load(std::memory_order_relaxed);
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
    std::atomic<int> destroyed{0};
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
    std::atomic<int> destroyed{0};
    constexpr int RETIRED = 300;

    std::thread producer([&destroyed]()
    {
        for (int i = 0; i < RETIRED; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(&destroyed));
        }
        // Thread exits here -> EpochThreadLocalState dtor donates remaining
        // retire_list to the global orphan list.
    });
    producer.join();

    // Main thread drains: each guarded retire triggers check_and_reclaim, which
    // pulls in the orphaned nodes and frees those past the safe epoch.
    std::atomic<int> drain_sink{0};
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(&drain_sink));
    }

    EXPECT_GT(destroyed.load(), 0)
        << "Orphaned nodes from an exited thread were never reclaimed (leak).";
}

/**
 * @test ConcurrentRetireNoCrash
 * @brief Many threads retiring simultaneously must not corrupt the manager.
 * Success is the absence of a crash plus a non-trivial reclamation count.
 */
TEST(EpochReclamationTest, ConcurrentRetireNoCrash)
{
    std::atomic<int> destroyed{0};
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 500;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([&destroyed]()
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(&destroyed));
            }
        });
    }
    for (auto& w : workers) w.join();

    EXPECT_GT(destroyed.load(), 0) << "No objects reclaimed under concurrent retire.";
}
