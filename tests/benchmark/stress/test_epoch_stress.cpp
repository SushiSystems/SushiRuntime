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
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

using namespace SushiRuntime::Memory;

namespace
{
    // Counters live on the heap, owned by shared_ptr. EBR reclamation is
    // deferred: a tail of retired objects can outlive the test's return and is
    // freed only during a later test's reclamation activity. Raw pointers to
    // stack atomics would then be written after the frame is gone — a
    // stack-use-after-return that silently corrupts whatever later test occupies
    // that stack region (it manifested as a crash in an unrelated test in the
    // full suite). Owning the counters by shared_ptr keeps them alive as long as
    // any Tracked still references them, so late dtor writes stay valid.
    struct Counters
    {
        std::atomic<long> live{0};       // net live objects (constructed - destroyed)
        std::atomic<long> destroyed{0};
    };
    using CountersPtr = std::shared_ptr<Counters>;

    inline CountersPtr make_counters() { return std::make_shared<Counters>(); }

    struct Tracked
    {
        CountersPtr c;
        explicit Tracked(CountersPtr counters) : c(std::move(counters))
        {
            c->live.fetch_add(1, std::memory_order_relaxed);
        }
        ~Tracked()
        {
            c->live.fetch_sub(1, std::memory_order_relaxed);
            c->destroyed.fetch_add(1, std::memory_order_relaxed);
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
    CountersPtr c = make_counters();

    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 20'000;

    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
    {
        workers.emplace_back([c]()
        {
            for (int i = 0; i < PER_THREAD; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(c));
            }
        });
    }
    for (auto& w : workers) w.join();

    // Drain on the main thread: advance epochs and pull in orphaned nodes.
    CountersPtr sink = make_counters();
    for (int i = 0; i < 4096; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(sink));
    }

    const long total = static_cast<long>(THREADS) * PER_THREAD;
    EXPECT_GT(c->destroyed.load(), total / 2)
        << "Most retired objects should have been reclaimed.";
    // Live count can never go negative (would mean a double-free).
    EXPECT_GE(c->live.load(), 0) << "Negative live count indicates a double-free.";
}

/**
 * @test ThreadSpawnExitHandoff
 * @brief Repeatedly spawn short-lived threads that retire then exit, forcing the
 * orphan-list handoff path on every join. A long-lived drainer thread keeps
 * advancing epochs. No crash and a growing destroyed count is the pass bar.
 */
TEST(EpochStress, ThreadSpawnExitHandoff)
{
    CountersPtr c = make_counters();

    constexpr int ROUNDS = 64;
    constexpr int PER_ROUND = 1000;

    std::atomic<bool> draining{true};
    CountersPtr drain = make_counters();
    std::thread drainer([&draining, drain]()
    {
        while (draining.load(std::memory_order_relaxed))
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(drain));
        }
    });

    for (int r = 0; r < ROUNDS; ++r)
    {
        std::thread shortlived([c]()
        {
            for (int i = 0; i < PER_ROUND; ++i)
            {
                EpochGuard guard;
                EpochManager::instance().retire(new Tracked(c));
            }
            // Thread exits here -> TLS dtor donates remaining nodes to orphans.
        });
        shortlived.join();
    }

    draining.store(false, std::memory_order_relaxed);
    drainer.join();

    EXPECT_GT(c->destroyed.load(), 0)
        << "Short-lived threads' orphaned retirees were never reclaimed.";
    EXPECT_GE(c->live.load(), 0) << "Negative live count indicates a double-free.";
}
