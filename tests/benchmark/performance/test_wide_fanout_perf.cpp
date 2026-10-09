/**************************************************************************/
/* test_wide_fanout_perf.cpp                                              */
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
#include <optional>
#include <vector>
#include <gtest/gtest.h>

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include "../../common/measurement.hpp"
#include "../../common/scheduler_fixture.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::make_scheduler;
using SushiRuntime::sushi_ptr;
using SushiRuntime::Test::CompletionSignal;
using SushiRuntime::Test::MeasurementLabel;
using SushiRuntime::Test::RunStatistics;

namespace
{
    /** @brief Holds the number of small steps one timed run drains back to back. */
    constexpr std::size_t K_STEP_COUNT = 20000;

    /** @brief Holds the number of independent tasks each step submits. */
    constexpr std::size_t K_TASKS_PER_STEP = 64;

    /** @brief Holds the number of tasks one timed run dispatches. */
    constexpr std::size_t K_TASK_COUNT = K_STEP_COUNT * K_TASKS_PER_STEP;

    /** @brief Holds the time one step may take to drain before the test fails. */
    constexpr std::chrono::seconds K_DRAIN_BUDGET{60};

    /**
     * @brief Bumps a shared counter and raises a signal when the counter reaches its target.
     */
    class CountingTask final : public Task
    {
        public:
            /**
             * @brief Builds a task that counts into @p counter.
             * @param counter Counter the task increments; must outlive the task.
             * @param target Count at which the task raises @p drained.
             * @param drained Signal raised by the task that reaches @p target; must outlive it.
             */
            CountingTask(std::atomic<std::size_t>& counter, std::size_t target,
                         CompletionSignal& drained)
                : counter_(counter), target_(target), drained_(drained)
            {
            }

            const char* get_name() const noexcept override
            {
                return "CountingTask";
            }

            ExecutionMode get_execution_mode() const override
            {
                return ExecutionMode::HOST;
            }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                if (counter_.fetch_add(1, std::memory_order_relaxed) + 1 == target_)
                    drained_.raise();
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override
            {
                return std::nullopt;
            }

            bool is_splittable() const override
            {
                return false;
            }

        private:
            /// @brief Counter shared by every task of the step.
            std::atomic<std::size_t>& counter_;
            /// @brief Count that marks the step as drained.
            std::size_t target_;
            /// @brief Signal the measuring thread waits on.
            CompletionSignal& drained_;
    };
}

TEST(Performance_WideFanout, ManySmallStepDrainLatency)
{
    using clock = std::chrono::steady_clock;

    RuntimeContext ctx;
    std::atomic<std::size_t> counter{0};

    // The signal and the tasks are declared before the scheduler, so they outlive its workers.
    CompletionSignal drained;
    std::vector<sushi_ptr<CountingTask>> tasks;
    tasks.reserve(K_TASKS_PER_STEP);
    for (std::size_t k = 0; k < K_TASKS_PER_STEP; ++k)
        tasks.push_back(
            SushiRuntime::make_sushi<CountingTask>(counter, K_TASKS_PER_STEP, drained));

    TaskScheduler sched = make_scheduler(ctx);
    const std::size_t workers = sched.get_worker_count();

    sched.start();
    sched.arm_for_execution(); // workers stay armed across all steps

    double best_step_ms  = 1e300;
    double worst_step_ms = 0.0;
    std::size_t drained_tasks = 0;
    bool every_step_drained = true;

    const RunStatistics per_task_ns = SushiRuntime::Test::measure([&]() -> double
    {
        if (!every_step_drained)
            return 0.0;

        double total_ms = 0.0;
        drained_tasks = 0;
        for (std::size_t s = 0; s < K_STEP_COUNT; ++s)
        {
            counter.store(0, std::memory_order_relaxed);
            drained.reset();

            // The vector keeps one reference per task, so the same tasks serve every step.
            const auto t0 = clock::now();
            for (const sushi_ptr<CountingTask>& task : tasks)
                sched.submit(task.get());
            const bool in_time = drained.wait_for(K_DRAIN_BUDGET);
            const auto t1 = clock::now();

            EXPECT_TRUE(in_time) << "step " << s << " did not drain in time";
            if (!in_time)
            {
                every_step_drained = false;
                return 0.0;
            }

            // The scheduler drops its references here, before the tasks are submitted again.
            sched.wait_quiescent();
            drained_tasks += counter.load(std::memory_order_relaxed);

            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_ms += ms;
            best_step_ms  = std::min(best_step_ms, ms);
            worst_step_ms = std::max(worst_step_ms, ms);
        }

        return (total_ms * 1e6) / double(K_TASK_COUNT);
    });

    sched.stop();
    ASSERT_TRUE(every_step_drained);
    ASSERT_EQ(drained_tasks, K_TASK_COUNT);

    const double avg_step_us = per_task_ns.median * double(K_TASKS_PER_STEP) / 1000.0;
    const double thr         = 1e9 / per_task_ns.median;

    std::printf("\n=== Wide fan-out, many small steps "
                "(%zu steps x %zu tasks, %zu workers, median of %zu runs) ===\n",
                K_STEP_COUNT, K_TASKS_PER_STEP, workers, per_task_ns.run_count);
    std::printf("  Step drain latency : avg %8.2f us   best %8.4f ms  worst %8.4f ms\n",
                avg_step_us, best_step_ms, worst_step_ms);
    std::printf("  Per-task           : %8.2f ns  (submit+steal+dispatch+drain)\n",
                per_task_ns.median);
    std::printf("  Throughput         : %10.0f tasks/s\n", thr);
    std::printf("  -> the step tail is where idle stealers scan empty peers (steal-1)\n"
                "     and where inflight_/counters are contended (sched-1). Attach\n"
                "     perf stat/c2c here for fence-count and cache-line attribution.\n");

    MeasurementLabel label;
    label.benchmark    = "wide_fanout";
    label.case_name    = "steps20000_x64";
    label.unit         = "ns_per_task";
    label.worker_count = workers;
    label.problem_size = K_TASK_COUNT;
    SushiRuntime::Test::print_measurement(label, per_task_ns);

    EXPECT_GT(thr, 0.0);
    EXPECT_EQ(drained_tasks, K_TASK_COUNT);
}
