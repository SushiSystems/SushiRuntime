/**************************************************************************/
/* benchmark_epoch_stress.cpp                                             */
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
 * @file benchmark_epoch_stress.cpp
 * @brief Tests epoch-based reclamation under multi-thread churn and thread spawn and exit.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
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
    /**
     * @brief Counters for tracking object lifecycles.
     * 
     * Counters are heap-owned by shared_ptr: EBR reclamation is deferred, so a
     * tail of retired objects outlives the test and is freed later. Raw pointers
     * to stack atomics would then be written after the frame is gone — a
     * stack-use-after-return. shared_ptr keeps them alive while any Tracked refers.
     */
    struct Counters
    {
        std::atomic<long> live{0};       // net live (constructed - destroyed)
        std::atomic<long> destroyed{0};
    };
    using CountersPtr = std::shared_ptr<Counters>;

    /**
     * @brief Creates a shared pointer to a new Counters object.
     * 
     * @return A shared pointer to the created counters.
     */
    inline CountersPtr make_counters() { return std::make_shared<Counters>(); }

    /**
     * @brief An object tracked by the epoch manager.
     */
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
 * @brief Tests high-churn epoch stress without leaking.
 * 
 * Sustained multi-thread churn: after all threads join and the main thread
 * drains, the destroyed count must dominate and live must stay bounded.
 */
TEST(Stress_EpochStress, HighChurnNoLeak)
{
    CountersPtr c = make_counters();

    constexpr int THREADS    = 8;
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
    for (auto& w : workers)
        w.join();

    // Drain on the main thread: advance epochs and pull in orphaned nodes.
    CountersPtr sink = make_counters();
    for (int i = 0; i < 4096; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(sink));
    }

    const long total = static_cast<long>(THREADS) * PER_THREAD;
    EXPECT_GT(c->destroyed.load(), total / 2) << "Most retired objects should have been reclaimed.";
    EXPECT_GE(c->live.load(), 0) << "Negative live count indicates a double-free.";
}

/**
 * @brief Tests orphan handoff by repeatedly spawning short-lived threads.
 * 
 * Repeatedly spawn short-lived threads that retire then exit, forcing the orphan
 * handoff on every join while a drainer keeps advancing epochs.
 */
TEST(Stress_EpochStress, ThreadSpawnExitHandoff)
{
    CountersPtr c = make_counters();

    constexpr int ROUNDS    = 64;
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

    EXPECT_GT(c->destroyed.load(), 0) << "Short-lived threads' orphaned retirees were never reclaimed.";
    EXPECT_GE(c->live.load(), 0) << "Negative live count indicates a double-free.";
}
