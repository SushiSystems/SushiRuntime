/**************************************************************************/
/* test_epoch_reclamation.cpp                                             */
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
 * @file test_epoch_reclamation.cpp
 * @brief Tests that epoch reclamation frees retired objects once and adopts dead threads' orphans.
 * @see tests/unit/README.md
 * @author Mustafa Garip
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

    /**
     * @brief Helper to create a shared atomic counter.
     *
     * @return A shared_ptr to a new atomic counter initialized to 0.
     */
    inline Counter make_counter() { return std::make_shared<std::atomic<int>>(0); }

    /** @brief Holds a heap payload that bumps a shared counter on destruction. */
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief Retires a count of objects, runs extra guarded passes and returns the number
     *        reclaimed.
     */
    int retire_and_drain(const Counter& destroyed, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        for (int i = 0; i < 512; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
        return destroyed->load(std::memory_order_relaxed);
    }
}

/**
 * @brief Tests that constructing and destroying an EpochGuard is safe and balanced.
 *
 * Checks that nested guards do not throw exceptions or leave unbalanced state.
 */
TEST(Unit_EpochReclamationTest, GuardEnterExitBalanced)
{
    EXPECT_NO_THROW({
        EpochGuard outer;
        {
            EpochGuard inner;
        }
    });
}

/**
 * @brief Tests that the bulk of retired objects are deleted after enough guarded retire calls.
 *
 * Verifies that the reclamation process eventually frees objects from past epochs.
 */
TEST(Unit_EpochReclamationTest, RetiredObjectsEventuallyReclaimed)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 1024;

    int reclaimed = retire_and_drain(destroyed, RETIRED);

    // Objects retired in the last epoch may survive, so the bound is loose; see README.md.
    EXPECT_GT(reclaimed, RETIRED) << "Epoch reclamation did not collect retired objects.";
}

/**
 * @brief Tests that a thread that retires then exits hands off its orphaned nodes.
 *
 * Its nodes go to the global orphan list and are reclaimed by another thread's draining work.
 */
TEST(Unit_EpochReclamationTest, ThreadExitHandsOffOrphans)
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
        // The exiting thread's destructor gives its retire list to the orphan list; see README.md.
    });
    producer.join();

    // Each guarded retire adopts orphans and frees those past the safe epoch; see README.md.
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
 * @brief Tests that many threads retiring at once do not corrupt the manager.
 *
 * Ensures concurrent retirements are thread-safe and objects are eventually reclaimed.
 */
TEST(Unit_EpochReclamationTest, ConcurrentRetireNoCrash)
{
    Counter destroyed = make_counter();
    constexpr int THREADS    = 8;
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
    for (auto& w : workers)
        w.join();

    EXPECT_GT(destroyed->load(), 0) << "No objects reclaimed under concurrent retire.";
}
