/**************************************************************************/
/* test_epoch_reclamation.cpp                                            */
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

// Unit coverage for epoch-based reclamation (EBR). EpochManager is a process-
// wide singleton, so these assert observable contracts: retired objects are
// eventually deleted once, EpochGuard enter/exit is balanced, and a thread that
// retires then exits hands its nodes to the global orphan list.

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

    // Heap payload that bumps a shared counter on destruction, so a test can
    // observe reclamation without reaching into EpochManager internals.
    //
    // The counter is a shared_ptr, NOT a raw pointer to a stack atomic: EBR
    // reclamation is deferred, so a tail of retired objects can be freed during a
    // later test. A raw stack pointer would then be written after this frame is
    // gone — a stack-use-after-return. shared_ptr keeps the counter alive as long
    // as any Tracked references it.
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    // Retire `count` objects, then run extra guarded passes to advance the epoch
    // and drain pending nodes. Returns how many were reclaimed.
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

// Constructing/destroying an EpochGuard must be safe and balanced, even nested.
TEST(Unit_EpochReclamationTest, GuardEnterExitBalanced)
{
    EXPECT_NO_THROW({
        EpochGuard outer;
        {
            EpochGuard inner;
        }
    });
}

// After enough guarded retire calls, the bulk of retired objects are deleted.
TEST(Unit_EpochReclamationTest, RetiredObjectsEventuallyReclaimed)
{
    Counter destroyed = make_counter();
    constexpr int RETIRED = 1024;

    int reclaimed = retire_and_drain(destroyed, RETIRED);

    // Most should be gone; survivors are those retired in the final, not-yet-
    // advanced epoch. The bound is generous to stay non-flaky.
    EXPECT_GT(reclaimed, RETIRED) << "Epoch reclamation did not collect retired objects.";
}

// A thread that retires then exits must not leak: its nodes go to the global
// orphan list and are reclaimed by another thread's draining work.
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
        // Thread exits here -> its dtor donates the remaining retire_list to the
        // global orphan list.
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

// Many threads retiring at once must not corrupt the manager.
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
