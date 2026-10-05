/****************************************************************/
/* test_wide_fanout_perf.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_wide_fanout_perf.cpp
 * @brief Measures per-step drain latency and per-task cost over many small scheduler steps.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
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
    bool wait_for(const std::atomic<std::size_t>& value, std::size_t target,
                  std::chrono::seconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::yield();
        }
        return true;
    }
}

TEST(Performance_WideFanout, ManySmallStepDrainLatency)
{
    constexpr std::size_t STEPS      = 20000; // small steps back-to-back
    constexpr std::size_t PER_STEP   = 64;    // independent tasks per step
    const std::size_t     TOTAL      = STEPS * PER_STEP;

    RuntimeContext ctx;
    TaskScheduler sched(ctx);
    const int workers = static_cast<int>(sched.get_worker_count());

    std::atomic<std::size_t> counter{0};

    sched.start();
    sched.arm_for_execution(); // workers stay armed across all steps

    double total_ms   = 0.0;
    double best_step_ms  = 1e300;
    double worst_step_ms = 0.0;

    using clock = std::chrono::steady_clock;
    const auto wall0 = clock::now();

    for (std::size_t s = 0; s < STEPS; ++s)
    {
        const std::size_t target = (s + 1) * PER_STEP;
        const auto t0 = clock::now();

        for (std::size_t k = 0; k < PER_STEP; ++k)
            sched.submit(new AtomicCounterTask(counter));

        // Each step drains to quiescence before the next starts; the idle-steal tail is measured.
        ASSERT_TRUE(wait_for(counter, target, std::chrono::seconds(60)))
            << "step " << s << " did not drain in time";

        const double ms =
            std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        total_ms += ms;
        best_step_ms  = std::min(best_step_ms, ms);
        worst_step_ms = std::max(worst_step_ms, ms);
    }

    const double wall_ms =
        std::chrono::duration<double, std::milli>(clock::now() - wall0).count();

    sched.stop();
    ASSERT_EQ(counter.load(), TOTAL);

    const double avg_step_us  = (total_ms / double(STEPS)) * 1000.0;
    const double per_task_ns  = (wall_ms * 1e6) / double(TOTAL);
    const double thr          = double(TOTAL) / (wall_ms / 1000.0);

    std::printf("\n=== Wide fan-out, many small steps (%zu steps x %zu tasks, %d workers) ===\n",
                STEPS, PER_STEP, workers);
    std::printf("  Step drain latency : avg %8.2f us   best %8.4f ms  worst %8.4f ms\n",
                avg_step_us, best_step_ms, worst_step_ms);
    std::printf("  Per-task           : %8.2f ns  (submit+steal+dispatch+drain)\n",
                per_task_ns);
    std::printf("  Throughput         : %10.0f tasks/s\n", thr);
    std::printf("  -> the step tail is where idle stealers scan empty peers (steal-1)\n"
                "     and where inflight_/counters are contended (sched-1). Attach\n"
                "     perf stat/c2c here for fence-count and cache-line attribution.\n");

    EXPECT_GT(thr, 0.0);
    EXPECT_EQ(counter.load(), TOTAL);
}
