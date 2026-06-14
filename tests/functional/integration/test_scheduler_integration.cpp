/**************************************************************************/
/* test_scheduler_integration.cpp                                       */
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
 * @file test_scheduler_integration.cpp
 * @brief Fast, deterministic scheduler lifecycle/correctness checks.
 *
 * The heavy load variants (1M task bomb, 8-producer contention, 500x lifecycle
 * thrash) live in benchmark/stress. This file keeps a small, CI-friendly slice:
 * the scheduler must execute exactly the tasks submitted, honor priority
 * bypass, and survive a normal arm -> submit -> stop round.
 */

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::AtomicCounterTask;
using SushiRuntime::Test::PriorityTrackingTask;

namespace
{
    /** @brief Block until @p value hits @p target, or fail past @p budget. */
    bool wait_for(const std::atomic<size_t>& value, size_t target,
                  std::chrono::seconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }
}

/**
 * @test ExecutesExactlySubmitted
 * @brief A modest batch of host tasks must all run exactly once.
 */
TEST(Integration_SchedulerIntegration, ExecutesExactlySubmitted)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::atomic<size_t> executed{0};
    constexpr size_t TOTAL = 2000;

    scheduler.start();
    scheduler.arm_for_execution();

    for (size_t i = 0; i < TOTAL; ++i)
        scheduler.submit(new AtomicCounterTask(executed));

    ASSERT_TRUE(wait_for(executed, TOTAL, std::chrono::seconds(10)))
        << "Scheduler did not drain a 2000-task batch in time.";

    scheduler.stop();
    EXPECT_EQ(executed.load(), TOTAL);
}

/**
 * @test PriorityBypass
 * @brief A late-submitted high-priority task must run among the first.
 * Deterministic small-scale mirror of the stress-suite bypass test.
 */
TEST(Integration_SchedulerIntegration, PriorityBypass)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::vector<int> order;
    std::mutex order_mutex;

    scheduler.start();

    for (int i = 0; i < 50; ++i)
        scheduler.submit(new PriorityTrackingTask(0, order, order_mutex));
    scheduler.submit(new PriorityTrackingTask(100, order, order_mutex));

    scheduler.arm_for_execution();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            if (order.size() >= 51) break;
        }
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    scheduler.stop();

    ASSERT_EQ(order.size(), 51u);

    int window = static_cast<int>(scheduler.num_workers) * 2;
    if (window > 51) window = 51;

    bool found_high_early = false;
    for (int i = 0; i < window; ++i)
        if (order[i] == 100) { found_high_early = true; break; }

    EXPECT_TRUE(found_high_early) << "High priority task missed the first batch.";
}

/**
 * @test CleanLifecycleRound
 * @brief A single arm/submit/stop round with no inflight work left must shut
 * down cleanly and report a fully drained queue.
 */
TEST(Integration_SchedulerIntegration, CleanLifecycleRound)
{
    RuntimeContext ctx;
    TaskScheduler scheduler(ctx);

    std::atomic<size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();
    for (int i = 0; i < 100; ++i)
        scheduler.submit(new AtomicCounterTask(executed));

    ASSERT_TRUE(wait_for(executed, 100, std::chrono::seconds(5)));
    EXPECT_NO_THROW({ scheduler.stop(); });
    EXPECT_EQ(executed.load(), 100u);
}
