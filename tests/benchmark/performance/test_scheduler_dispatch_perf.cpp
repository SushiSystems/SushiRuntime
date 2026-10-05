/****************************************************************/
/* test_scheduler_dispatch_perf.cpp                             */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_scheduler_dispatch_perf.cpp
 * @brief Measures the per-task cost of the work-stealing scheduler against an inline loop.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
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
    constexpr size_t TOTAL  = 500000; // tasks per timed run
    constexpr int    ROUNDS = 3;      // best-of-N to strip jitter

    /**
     * @brief Wait for an atomic value to reach a target within a budget.
     * 
     * @param value The atomic value to watch.
     * @param target The target value.
     * @param budget The time budget.
     * @return True if the target was reached, false on timeout.
     */
    bool wait_for(const std::atomic<size_t>& value, size_t target,
                  std::chrono::seconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    /**
     * @brief Measures nanoseconds spent on TOTAL inline increments.
     * 
     * This represents the cost of the raw work.
     * 
     * @param counter The atomic counter to increment.
     * @return The time in nanoseconds.
     */
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
 * @brief Tests per-task scheduler dispatch overhead against an inline baseline.
 * 
 * Both sides do the identical work, and the delta is the amortized submit+steal+dispatch cost.
 */
TEST(Performance_SchedulerDispatchBench, DispatchOverheadVsInlineBaseline)
{
    RuntimeContext ctx;
    std::atomic<size_t> counter{0};

    // Baseline: inline work, best-of-N.
    double best_base_ns = 1e300;
    for (int r = 0; r < ROUNDS; ++r)
        best_base_ns = std::min(best_base_ns, baseline_ns(counter));
    ASSERT_EQ(counter.load(), TOTAL); // consume: defeats dead-code elimination
    const double base_per_task_ns = best_base_ns / double(TOTAL);

    // Scheduler: same work through the dispatch path, best-of-N.
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

    EXPECT_EQ(counter.load(), TOTAL);
    EXPECT_GT(thr, 0.0);
}
