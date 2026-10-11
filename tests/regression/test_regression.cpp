/**************************************************************************/
/* test_regression.cpp                                                    */
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
 * @file test_regression.cpp
 * @brief Tests one fast, deterministic sentinel per defect that was found and fixed.
 * @see tests/regression/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "test_helpers.hpp"
#include "scheduler_fixture.hpp"
#include "test_context.hpp"

using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Scheduler;
using SushiRuntime::Test::make_scheduler;
using namespace SushiRuntime::Internal::Memory;
using SushiRuntime::Test::AtomicCounterTask;

/**
 * @brief Tests that stop() right after submit drains cleanly.
 *
 * Prevents deadlocks or leaked threads when stopping the scheduler immediately
 * after queuing tasks.
 */
TEST(Regression_Sentinel, Scheduler_AbruptStopDrains)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    RuntimeContext& ctx = *ctx_owner;
    TaskScheduler scheduler = make_scheduler(ctx);
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
    // The counter is a shared_ptr, as reclamation frees Tracked after this frame; see README.md.
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


/**
 * @brief Tests that a thread that outlives the Runtime it allocated from tears down without a
 *        fault.
 */
TEST(Regression_Sentinel, AThreadOutlivingTheRuntimeTearsDownCleanly)
{
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool allocated = false;
    bool runtime_gone = false;

    std::thread outliver;

    {
        auto rt = SushiRuntime::Runtime::create().value();

        outliver = std::thread([&]()
        {
            // The thread's exit-time destructors touch per-thread runtime state; see README.md.
            {
                auto scratch = rt.buffer<int>(4096).value();
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
    }   // Runtime destroyed here, while the other thread is still alive.

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        runtime_gone = true;
    }
    gate.notify_one();

    // Reaching the line after this join without a fault is the assertion; see README.md.
    outliver.join();
    SUCCEED() << "a thread unwound after the Runtime it allocated from was gone";
}
