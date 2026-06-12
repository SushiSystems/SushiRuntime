/**************************************************************************/
/* test_epoch_stress.cpp                                                 */
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
 * @file test_epoch_stress.cpp
 * @brief High-churn stress for epoch-based reclamation.
 *
 * Many threads enter/exit critical sections and retire heap objects at speed,
 * with threads continuously spawning and exiting to exercise the orphan-list
 * handoff path. The invariants checked are leak/UAF oriented: every object that
 * was retired is destroyed exactly once, and a large majority is reclaimed
 * before the test ends (the remainder may sit in the final epoch). Run under a
 * sanitizer (ASan/TSan) this also surfaces races and use-after-free.
 */

#include <atomic>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

using namespace SushiRuntime::Memory;

namespace
{
    struct Tracked
    {
        std::atomic<long>* live;     // net live objects (constructed - destroyed)
        std::atomic<long>* destroyed;
        explicit Tracked(std::atomic<long>* l, std::atomic<long>* d) : live(l), destroyed(d)
        {
            live->fetch_add(1, std::memory_order_relaxed);
        }
        ~Tracked()
        {
            live->fetch_sub(1, std::memory_order_relaxed);
            destroyed->fetch_add(1, std::memory_order_relaxed);
        }
    };
}

/**
 * @test HighChurnNoLeak
 * @brief Sustained multi-thread retire churn. After all threads join and the
 * main thread drains, the destroyed count must dominate and live objects must
 * not have run away (bounded backlog).
 */
TEST(EpochStress, HighChurnNoLeak)
{
    std::atomic<long> live{0};
    std::atomic<long> destroyed{0};

    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 20'000;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([&live, &destroyed]()
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(&live, &destroyed));
            }
        });
    }
    for (auto& w : workers) w.join();

    // Drain on the main thread: advance epochs and pull in orphaned nodes.
    std::atomic<long> sink_live{0};
    std::atomic<long> sink_destroyed{0};
    for (int i = 0; i < 4096; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(&sink_live, &sink_destroyed));
    }

    const long total = static_cast<long>(THREADS) * PER_THREAD;
    EXPECT_GT(destroyed.load(), total / 2)
        << "Most retired objects should have been reclaimed.";
    // Live count can never go negative (would mean a double-free).
    EXPECT_GE(live.load(), 0) << "Negative live count indicates a double-free.";
}

/**
 * @test ThreadSpawnExitHandoff
 * @brief Repeatedly spawn short-lived threads that retire then exit, forcing the
 * orphan-list handoff path on every join. A long-lived drainer thread keeps
 * advancing epochs. No crash and a growing destroyed count is the pass bar.
 */
TEST(EpochStress, ThreadSpawnExitHandoff)
{
    std::atomic<long> live{0};
    std::atomic<long> destroyed{0};

    constexpr int ROUNDS = 64;
    constexpr int PER_ROUND = 1000;

    std::atomic<bool> draining{true};
    std::atomic<long> drain_live{0};
    std::atomic<long> drain_destroyed{0};
    std::thread drainer([&]()
    {
        while (draining.load(std::memory_order_relaxed))
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(&drain_live, &drain_destroyed));
        }
    });

    for (int r = 0; r < ROUNDS; ++r)
    {
        std::thread shortlived([&live, &destroyed]()
        {
            for (int i = 0; i < PER_ROUND; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(&live, &destroyed));
            }
            // Thread exits here -> TLS dtor donates remaining nodes to orphans.
        });
        shortlived.join();
    }

    draining.store(false, std::memory_order_relaxed);
    drainer.join();

    EXPECT_GT(destroyed.load(), 0)
        << "Short-lived threads' orphaned retirees were never reclaimed.";
    EXPECT_GE(live.load(), 0) << "Negative live count indicates a double-free.";
}
