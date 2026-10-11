/**************************************************************************/
/* test_scheduler_integration.cpp                                         */
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
 * @file test_scheduler_integration.cpp
 * @brief Tests the scheduler for exact task execution, priority bypass and a clean stop.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include "test_helpers.hpp"
#include "scheduler_fixture.hpp"

using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Scheduler;
using SushiRuntime::Test::make_scheduler;
using SushiRuntime::Test::AtomicCounterTask;
using SushiRuntime::Test::PriorityTrackingTask;

namespace
{
    /**
     * @brief Blocks until value hits target, or fails past the budget.
     * @param value The atomic value to wait for.
     * @param target The target value.
     * @param budget The maximum duration to wait.
     * @return True if the target was reached, false if it timed out.
     */
    bool wait_for(const std::atomic<size_t>& value, size_t target,
                  std::chrono::seconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }
}

/**
 * @brief Tests that a modest batch of host tasks runs exactly once.
 *
 * Verifies that the scheduler properly executes all submitted tasks.
 */
TEST(Integration_SchedulerIntegration, ExecutesExactlySubmitted)
{
    RuntimeContext ctx;
    TaskScheduler scheduler = make_scheduler(ctx);

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
 * @brief Tests that a late high-priority task bypasses lower priority tasks.
 *
 * Verifies that the scheduler honors task priorities and executes the high priority task among the first.
 */
TEST(Integration_SchedulerIntegration, PriorityBypass)
{
    RuntimeContext ctx;
    TaskScheduler scheduler = make_scheduler(ctx);

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
            if (order.size() >= 51)
                break;
        }
        if (std::chrono::steady_clock::now() > deadline)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    scheduler.stop();

    ASSERT_EQ(order.size(), 51u);

    int window = static_cast<int>(scheduler.get_worker_count()) * 2;
    if (window > 51)
        window = 51;

    bool found_high_early = false;
    for (int i = 0; i < window; ++i)
    {
        if (order[i] == 100)
        {
            found_high_early = true;
            break;
        }
    }

    EXPECT_TRUE(found_high_early) << "High priority task missed the first batch.";
}

/**
 * @brief Tests that a single lifecycle round shuts down cleanly.
 *
 * Verifies that a clean arm -> submit -> stop round with no inflight work exits without hanging.
 */
TEST(Integration_SchedulerIntegration, CleanLifecycleRound)
{
    RuntimeContext ctx;
    TaskScheduler scheduler = make_scheduler(ctx);

    std::atomic<size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();
    for (int i = 0; i < 100; ++i)
        scheduler.submit(new AtomicCounterTask(executed));

    ASSERT_TRUE(wait_for(executed, 100, std::chrono::seconds(5)));
    EXPECT_NO_THROW({ scheduler.stop(); });
    EXPECT_EQ(executed.load(), 100u);
}
