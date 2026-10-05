/****************************************************************/
/* test_wide_fanout_perf.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Gate benchmark for the deque-steal fast path (audit steal-1) and the global
// quiescence counter (audit sched-1), profiled in the substrate's characteristic
// shape: MANY SMALL steps rather than one long sustained stream. Each step submits a
// modest batch of independent tasks and then drains to quiescence; the tail of every
// step is exactly where workers idle-spin stealing from empty peers (steal-1 removes
// a seq_cst fence from each empty attempt) and where inflight_ / the shared counters
// are hammered (sched-1's contention target).
//
// We report per-step drain latency and per-task ns. This isolates the fixed
// per-step overhead that dominates the substrate, complementing
// test_scheduler_dispatch_perf's single sustained stream. Note: fully attributing
// steal-1's fence removal and sched-1's line contention needs hardware counters
// (perf stat / c2c); this benchmark is the portable throughput+latency guard and the
// harness those counters attach to. Asserts correctness + forward progress only.

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

        // Drain this step to quiescence before starting the next — the idle-steal
        // tail is the point of interest.
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
