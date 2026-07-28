/**************************************************************************/
/* test_regression.cpp                                                   */
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

// Sentinels for previously fixed bugs — one fast, deterministic test per defect
// so a future change cannot silently reintroduce it.

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/SushiRuntime.h>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using namespace SushiRuntime::Memory;
using SushiRuntime::Test::AtomicCounterTask;

/**
 * @brief Tests that stop() right after submit drains cleanly.
 *
 * Prevents deadlocks or leaked threads when stopping the scheduler immediately
 * after queuing tasks.
 */
TEST(Regression_Sentinel, Scheduler_AbruptStopDrains)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);
    std::atomic<size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();

    for (int i = 0; i < 1000; ++i)
        scheduler.submit(new AtomicCounterTask(executed));

    EXPECT_NO_THROW({ scheduler.stop(); })
        << "Abrupt stop() must drain without deadlock or throw.";
}

/**
 * @brief Tests that orphaned nodes from an exited thread are correctly reclaimed.
 *
 * Simulates a thread exiting with a retire list, which is then donated to the
 * global orphan list and must be freed by survivors. A prior bug returned
 * orphans to the wrong free pool.
 */
TEST(Regression_Sentinel, Epoch_OrphanedNodesReclaimed)
{
    // The counter is a shared_ptr, not a raw pointer to a stack atomic: EBR
    // reclamation is deferred, so a tail of retired objects outlives this test
    // and is freed during a later test. A raw stack pointer would then be written
    // after the frame is gone (a stack-use-after-return). shared_ptr keeps the
    // counter alive as long as any Tracked refers to it.
    using Counter = std::shared_ptr<std::atomic<int>>;
    struct Tracked
    {
        Counter destroyed;
        explicit Tracked(Counter d) : destroyed(std::move(d)) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    Counter destroyed = std::make_shared<std::atomic<int>>(0);

    std::thread producer([destroyed]()
    {
        for (int i = 0; i < 300; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(destroyed));
        }
    });
    producer.join();

    Counter sink = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(sink));
    }

    EXPECT_GT(destroyed->load(), 0)
        << "Orphaned nodes from an exited thread were not reclaimed (leak regression).";
}


// A thread that allocated through a Runtime, and is still alive when that
// Runtime is destroyed, flushes its ThreadLocalMagazine at thread exit — into an
// allocator that no longer exists. That is the shape a long-running simulation
// process hits (TODO #29): a worker outliving the context it allocated from.
//
// The guarantee under test is that the flush becomes a no-op instead of touching
// freed pages: the allocator's destructor holds the flush lock exclusively while
// it clears its registry slot, so a later flush finds a null slot and returns.
// Without it this test is a use-after-free — visible under ASan, and a crash
// often enough without it.
TEST(Regression_Sentinel, MagazineFlushSurvivesRuntimeTeardown)
{
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool allocated = false;
    bool runtime_gone = false;

    std::thread outliver;

    {
        auto rt = SushiRuntime::Runtime::create();

        outliver = std::thread([&]()
        {
            // Allocate and free enough to populate this thread's magazine, so its
            // exit-time flush has something to hand back.
            {
                auto scratch = rt.buffer<int>(4096);
                for (std::size_t i = 0; i < 64; ++i)
                    scratch[i] = static_cast<int>(i);
            }

            {
                std::lock_guard<std::mutex> lock(gate_mutex);
                allocated = true;
            }
            gate.notify_one();

            // Stay alive until the Runtime — and its allocator — are gone.
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate.wait(lock, [&] { return runtime_gone; });
        });

        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return allocated; });
    }   // Runtime destroyed here, while the other thread still holds a magazine.

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        runtime_gone = true;
    }
    gate.notify_one();

    // The thread's magazine flushes as it exits. Reaching this line without a
    // fault IS the assertion; the EXPECT documents that we got here on purpose.
    outliver.join();
    SUCCEED() << "a magazine flushed after its allocator was destroyed";
}
