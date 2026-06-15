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
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using namespace SushiRuntime::Memory;
using SushiRuntime::Test::AtomicCounterTask;

// stop() right after submit, without waiting, must drain cleanly instead of
// deadlocking or leaking threads.
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

// When a thread exits, its retire_list is donated to the global orphan list and
// reclaimed by survivors. A prior bug returned orphans to the wrong free pool.
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
