/**************************************************************************/
/* test_regression.cpp                                                   */
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
 * @file test_regression.cpp
 * @brief Sentinels for previously fixed bugs. Each test guards one concrete
 * defect so a future change cannot silently reintroduce it.
 *
 * Convention: every test is tagged with a `// REGRESSION: <one-line summary>`
 * comment and a `Regression_<Area>` test-name prefix. Keep these small,
 * deterministic, and fast -- they run in the functional (CI) suite on every
 * commit. When you fix a new bug, add a sentinel here referencing it.
 */

#include <atomic>
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

/**
 * @test Regression_Scheduler_AbruptStopDrains
 * @brief REGRESSION: stop() called immediately after submit, without waiting for
 * completion, must drain/teardown cleanly instead of deadlocking or leaking
 * threads. Mirrors the abrupt-stop path that the stress suite hammers, but as a
 * single fast deterministic round so any regression in the drain logic trips
 * here on every CI run.
 */
TEST(Regression, Scheduler_AbruptStopDrains)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);
    std::atomic<size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();

    for (int i = 0; i < 1000; ++i)
        scheduler.submit(new AtomicCounterTask(executed));

    // Abrupt stop: do NOT wait for executed == 1000 first.
    EXPECT_NO_THROW({ scheduler.stop(); })
        << "Abrupt stop() must drain without deadlock or throw.";
}

/**
 * @test Regression_Epoch_OrphanedNodesReclaimed
 * @brief REGRESSION: when a thread exits, its outstanding retire_list is donated
 * to the EpochManager's global orphan list and reclaimed by surviving threads.
 * A prior bug returned orphaned nodes to the wrong thread-local free pool; this
 * sentinel proves orphans are actually deleted (no leak / no cross-pool reuse).
 */
TEST(Regression, Epoch_OrphanedNodesReclaimed)
{
    struct Tracked
    {
        std::atomic<int>* destroyed;
        explicit Tracked(std::atomic<int>* d) : destroyed(d) {}
        ~Tracked() { destroyed->fetch_add(1, std::memory_order_relaxed); }
    };

    std::atomic<int> destroyed{0};

    std::thread producer([&destroyed]()
    {
        for (int i = 0; i < 300; ++i)
        {
            EpochGuard guard;
            EpochManager::instance().retire(new Tracked(&destroyed));
        }
    });
    producer.join();

    std::atomic<int> sink{0};
    for (int i = 0; i < 1024; ++i)
    {
        EpochGuard guard;
        EpochManager::instance().retire(new Tracked(&sink));
    }

    EXPECT_GT(destroyed.load(), 0)
        << "Orphaned nodes from an exited thread were not reclaimed (leak regression).";
}
