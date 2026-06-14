/**************************************************************************/
/* test_scheduler_dispatch_perf.cpp                                       */
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
 * @file test_scheduler_dispatch_perf.cpp
 * @brief Measures the scheduler's per-task dispatch overhead honestly.
 *
 * FAIRNESS DESIGN. The two sides do the *identical* unit of work — a single
 * relaxed atomic increment — so the only difference being measured is the path
 * the work travels:
 *   * baseline : the increment called inline in a tight loop (the irreducible
 *                cost of the work itself; establishes the floor).
 *   * scheduler: the same increment wrapped in an AtomicCounterTask, submitted
 *                to the work-stealing scheduler and drained.
 * The delta (scheduler_per_task - baseline_per_task) is the amortized cost the
 * scheduler adds: submit + queueing + steal + dispatch.
 *
 * We deliberately do NOT assert "scheduler is faster": the baseline is single
 * threaded while the scheduler is parallel, so a speed comparison would be
 * apples-to-oranges. The benchmark reports the overhead number and only asserts
 * correctness (every task ran) and forward progress (non-zero throughput, no
 * hang) — the same philosophy as the stress suite. Each side is timed best-of-N
 * to strip OS jitter, and the counter is consumed (asserted) so neither loop can
 * be optimized away.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::AtomicCounterTask;

namespace
{
    constexpr size_t TOTAL    = 500000; // tasks per timed run
    constexpr int    ROUNDS   = 3;      // best-of-N to strip jitter

    bool wait_for(const std::atomic<size_t>& value, size_t target,
                  std::chrono::seconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    // ns spent on TOTAL inline increments — the cost of the raw work.
    double baseline_ns(std::atomic<size_t>& counter)
    {
        counter.store(0, std::memory_order_relaxed);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < TOTAL; ++i)
            counter.fetch_add(1, std::memory_order_relaxed);
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::nano>(t1 - t0).count();
    }
}

/**
 * @test DispatchOverheadVsInlineBaseline
 * @brief Reports amortized per-task scheduler overhead against the inline floor.
 */
TEST(Performance_SchedulerDispatchBench, DispatchOverheadVsInlineBaseline)
{
    RuntimeContext ctx;
    std::atomic<size_t> counter{0};

    // --- baseline: inline work, best-of-N -----------------------------------
    double best_base_ns = 1e300;
    for (int r = 0; r < ROUNDS; ++r)
        best_base_ns = std::min(best_base_ns, baseline_ns(counter));
    ASSERT_EQ(counter.load(), TOTAL); // consume: defeats dead-code elimination
    const double base_per_task_ns = best_base_ns / double(TOTAL);

    // --- scheduler: same work through the dispatch path, best-of-N -----------
    double best_sched_ms = 1e300;
    int    workers       = 0;
    for (int r = 0; r < ROUNDS; ++r)
    {
        TaskScheduler sched(ctx);
        workers = static_cast<int>(sched.get_worker_count());
        counter.store(0, std::memory_order_relaxed);

        sched.start();
        sched.arm_for_execution();

        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < TOTAL; ++i)
            sched.submit(new AtomicCounterTask(counter));

        ASSERT_TRUE(wait_for(counter, TOTAL, std::chrono::seconds(60)))
            << "Scheduler did not drain " << TOTAL << " tasks in time.";
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        best_sched_ms = std::min(best_sched_ms, ms);

        sched.stop();
        ASSERT_EQ(counter.load(), TOTAL); // consume
    }

    const double sched_per_task_ns = (best_sched_ms * 1e6) / double(TOTAL);
    const double overhead_ns       = sched_per_task_ns - base_per_task_ns;
    const double thr               = double(TOTAL) / (best_sched_ms / 1000.0);

    std::printf("\n=== Scheduler dispatch overhead (%zu tasks, %d workers) ===\n",
                TOTAL, workers);
    std::printf("  Inline baseline : %8.2f ns/task\n", base_per_task_ns);
    std::printf("  Scheduler path  : %8.2f ns/task  (wall %.0f ns amortized)\n",
                sched_per_task_ns, sched_per_task_ns);
    std::printf("  Dispatch overhead: %7.2f ns/task (submit+steal+dispatch)\n",
                overhead_ns);
    std::printf("  Throughput      : %10.0f tasks/s\n", thr);

    // Honest assertions only: work happened and the pipeline made progress.
    EXPECT_EQ(counter.load(), TOTAL);
    EXPECT_GT(thr, 0.0);
}
