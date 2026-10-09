/**************************************************************************/
/* test_scheduler_dispatch_perf.cpp                                       */
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
 * @file test_scheduler_dispatch_perf.cpp
 * @brief Measures the per-task cost of the work-stealing scheduler against an inline loop.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

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
    /** @brief Holds the number of tasks one timed run dispatches. */
    constexpr std::size_t K_TASK_COUNT = 500000;

    /** @brief Holds the time one timed run may take to drain before the test fails. */
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
            /// @brief Counter shared by every task of the run.
            std::atomic<std::size_t>& counter_;
            /// @brief Count that marks the run as drained.
            std::size_t target_;
            /// @brief Signal the measuring thread waits on.
            CompletionSignal& drained_;
    };

    /**
     * @brief Measures nanoseconds spent on K_TASK_COUNT inline increments.
     *
     * This represents the cost of the raw work.
     *
     * @param counter The atomic counter to increment.
     * @return The time in nanoseconds.
     */
    double baseline_ns(std::atomic<std::size_t>& counter)
    {
        counter.store(0, std::memory_order_relaxed);
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < K_TASK_COUNT; ++i)
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
    std::atomic<std::size_t> counter{0};

    const RunStatistics baseline = SushiRuntime::Test::measure(
        [&counter] { return baseline_ns(counter) / double(K_TASK_COUNT); });
    ASSERT_EQ(counter.load(), K_TASK_COUNT); // consume: defeats dead-code elimination

    // The signal and the tasks are declared before any scheduler, so they outlive its workers.
    CompletionSignal drained;
    std::vector<sushi_ptr<CountingTask>> tasks;
    tasks.reserve(K_TASK_COUNT);
    for (std::size_t i = 0; i < K_TASK_COUNT; ++i)
        tasks.push_back(SushiRuntime::make_sushi<CountingTask>(counter, K_TASK_COUNT, drained));

    std::size_t workers = 0;
    bool every_run_drained = true;
    const RunStatistics scheduled = SushiRuntime::Test::measure([&]() -> double
    {
        if (!every_run_drained)
            return 0.0;

        TaskScheduler sched = make_scheduler(ctx);
        workers = sched.get_worker_count();
        counter.store(0, std::memory_order_relaxed);
        drained.reset();

        sched.start();
        sched.arm_for_execution();

        // The vector keeps one reference per task, so the scheduler's release never deletes one.
        const auto t0 = std::chrono::steady_clock::now();
        for (const sushi_ptr<CountingTask>& task : tasks)
            sched.submit(task.get());
        const bool in_time = drained.wait_for(K_DRAIN_BUDGET);
        const auto t1 = std::chrono::steady_clock::now();

        sched.stop();

        EXPECT_TRUE(in_time) << "Scheduler did not drain " << K_TASK_COUNT << " tasks in time.";
        EXPECT_EQ(counter.load(), K_TASK_COUNT); // consume
        every_run_drained = in_time && counter.load() == K_TASK_COUNT;

        return std::chrono::duration<double, std::nano>(t1 - t0).count() / double(K_TASK_COUNT);
    });
    ASSERT_TRUE(every_run_drained);

    const double overhead_ns = scheduled.median - baseline.median;
    const double run_wall_ms = scheduled.median * double(K_TASK_COUNT) / 1e6;
    const double thr         = double(K_TASK_COUNT) / (run_wall_ms / 1000.0);

    std::printf("\n=== Scheduler dispatch overhead "
                "(%zu tasks, %zu workers, median of %zu runs) ===\n",
                K_TASK_COUNT, workers, scheduled.run_count);
    std::printf("  Inline baseline : %8.2f ns/task\n", baseline.median);
    std::printf("  Scheduler path  : %8.2f ns/task  (%.2f ms wall per run)\n",
                scheduled.median, run_wall_ms);
    std::printf("  Dispatch overhead: %7.2f ns/task (submit+steal+dispatch)\n",
                overhead_ns);
    std::printf("  Throughput      : %10.0f tasks/s\n", thr);

    MeasurementLabel label;
    label.benchmark    = "scheduler_dispatch";
    label.unit         = "ns_per_task";
    label.worker_count = workers;
    label.problem_size = K_TASK_COUNT;

    label.case_name = "inline_baseline";
    SushiRuntime::Test::print_measurement(label, baseline);
    label.case_name = "scheduler";
    SushiRuntime::Test::print_measurement(label, scheduled);

    EXPECT_EQ(counter.load(), K_TASK_COUNT);
    EXPECT_GT(thr, 0.0);
}
