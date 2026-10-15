/**************************************************************************/
/* benchmark_scheduler_execution_engine.cpp                               */
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
 * @file benchmark_scheduler_execution_engine.cpp
 * @brief Measures graph compilation, dispatch, stealing and fan-out of the scheduler.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp> // CompiledGraph holds sushi_ptr<Node>; dtor needs full type
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>
#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/measurement/measurement.hpp>
#include <SushiRuntime/measurement/completion_signal.hpp>
#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>

using namespace SushiRuntime;

/** @brief Measures how TaskGraph::compile() scales and what overlapping transfers with compute saves. */
namespace GraphCompileTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Graph;

    namespace
    {
        constexpr int K_ROUNDS = 3;

        /**
         * @brief Measures the minimum compilation time for a task graph.
         * 
         * @param nodes The number of nodes in the graph.
         * @return The minimum compilation time in microseconds.
         */
        double min_compile_us(size_t nodes)
        {
            // Task t reads buf[t % M] and writes buf[(t + 1) % M], so consecutive tasks depend.
            constexpr size_t M = 16;
            std::vector<int> bufs(M, 0);

            double best = 1e300;
            for (int r = 0; r < K_ROUNDS; ++r)
            {
                TaskGraph graph;
                for (size_t t = 0; t < nodes; ++t)
                {
                    void* in  = &bufs[t % M];
                    void* out = &bufs[(t + 1) % M];
                    TaskMetadata meta{"n"};
                    graph.add_task(meta, {SushiRuntime::BufferRegion(in)}, {SushiRuntime::BufferRegion(out)},
                        [](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event { return {}; });
                }

                const auto t0 = std::chrono::steady_clock::now();
                auto compiled = graph.compile().value();
                const double us = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - t0).count();
                EXPECT_GE(compiled.roots.size(), 1u); // consume: compile must produce a DAG
                best = std::min(best, us);
            }
            return best;
        }
    }

    /**
     * @brief Tests TaskGraph::compile() latency scaling.
     * 
     * Measures compile() latency and us/node across increasing graph sizes.
     */
    TEST(Performance_GraphCompileBench, CompileScaling)
    {
        const size_t sizes[] = { 64, 256, 1024, 4096, 16384 };

        std::printf("\n=== TaskGraph::compile() scaling ===\n");
        std::printf("  %8s | %12s | %10s\n", "nodes", "compile (us)", "us/node");
        for (size_t n : sizes)
        {
            const double us = min_compile_us(n);
            std::printf("  %8zu | %12.1f | %10.3f\n", n, us, us / double(n));
            EXPECT_GT(us, 0.0);
        }
    }

    // H2D / D2H transfer-compute overlap.
    namespace
    {
        constexpr size_t K_ELEMS  = 1u << 20; // 4 MiB of floats
        constexpr int    K_INNER  = 64;       // per-element compute weight
        constexpr int    K_CHUNKS = 8;

        /**
         * @brief Reference compute per element, matching the device kernel.
         * 
         * @param x The input value.
         * @return The computed result.
         */
        float expected(float x)
        {
            for (int k = 0; k < K_INNER; ++k)
                x = x * 1.001f + 0.5f;
            return x;
        }

        /**
         * @brief Run a function multiple times and return the best execution time.
         * 
         * @param fn The function to run.
         * @return The best time in milliseconds.
         */
        double best_of(const std::function<double()>& fn)
        {
            double best = 1e300;
            for (int r = 0; r < K_ROUNDS; ++r)
                best = std::min(best, fn());
            return best;
        }
    }

    /**
     * @brief Tests H2D/D2H transfer-compute overlap.
     * 
     * Compares sequential (copy -> compute -> copy) vs a chunked pipeline across two queues.
     */
    TEST(Performance_GraphCompileBench, TransferComputeOverlapSpeedup)
    {
        if (!SushiRuntime::Test::has_any_sycl_device())
            GTEST_SKIP() << "SYCL reports no device.";

        auto ctx = SushiRuntime::Test::make_context();

        sycl::queue& base = ctx->devices().queue(0);
        sycl::device dev  = base.get_device();
        sycl::context cxt = base.get_context();
        sycl::queue qa(cxt, dev, sycl::property::queue::in_order{});
        sycl::queue qb(cxt, dev, sycl::property::queue::in_order{});

        const size_t bytes = K_ELEMS * sizeof(float);
        float* host_in  = sycl::malloc_host<float>(K_ELEMS, qa);
        float* host_out = sycl::malloc_host<float>(K_ELEMS, qa);
        float* d        = sycl::malloc_device<float>(K_ELEMS, qa);
        ASSERT_NE(host_in, nullptr);
        ASSERT_NE(host_out, nullptr);
        ASSERT_NE(d, nullptr);
        for (size_t i = 0; i < K_ELEMS; ++i)
            host_in[i] = float(i % 97);

        auto compute = [](sycl::queue& q, float* p, size_t off, size_t n, sycl::event dep) -> sycl::event
        {
            return q.parallel_for(sycl::range<1>(n), dep, [=](sycl::id<1> i)
            {
                float x = p[off + i];
                for (int k = 0; k < K_INNER; ++k)
                    x = x * 1.001f + 0.5f;
                p[off + i] = x;
            });
        };

        // Sequential: one blocking copy -> compute -> copy.
        auto run_sequential = [&]() -> double
        {
            const auto t0 = std::chrono::steady_clock::now();
            qa.memcpy(d, host_in, bytes).wait();
            compute(qa, d, 0, K_ELEMS, {}).wait();
            qa.memcpy(host_out, d, bytes).wait();
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        };

        // Overlapped: chunks pipelined across two in-order queues.
        auto run_overlapped = [&]() -> double
        {
            const size_t chunk = (K_ELEMS + K_CHUNKS - 1) / K_CHUNKS;
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<sycl::event> tail;
            for (int c = 0; c < K_CHUNKS; ++c)
            {
                const size_t off = size_t(c) * chunk;
                if (off >= K_ELEMS)
                    break;
                const size_t n = std::min(chunk, K_ELEMS - off);
                sycl::queue& q = (c & 1) ? qb : qa;
                auto e1 = q.memcpy(d + off, host_in + off, n * sizeof(float));
                auto e2 = compute(q, d, off, n, e1);
                auto e3 = q.memcpy(host_out + off, d + off, n * sizeof(float), e2);
                tail.push_back(e3);
            }
            for (auto& e : tail)
                e.wait();
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        };

        const double seq = best_of(run_sequential);
        bool ok = true;
        for (size_t i = 0; i < K_ELEMS; ++i)
        {
            if (std::fabs(host_out[i] - expected(host_in[i])) > 1e-1f)
            {
                ok = false;
                break;
            }
        }

        const double ovl = best_of(run_overlapped);
        bool ok2 = true;
        for (size_t i = 0; i < K_ELEMS; ++i)
        {
            if (std::fabs(host_out[i] - expected(host_in[i])) > 1e-1f)
            {
                ok2 = false;
                break;
            }
        }

        const double speedup = ovl > 0 ? seq / ovl : 0.0;
        std::printf("\n=== H2D/D2H transfer-compute overlap (%zu elems, %d chunks) ===\n",
                    K_ELEMS, K_CHUNKS);
        std::printf("  Sequential : %.3f ms\n", seq);
        std::printf("  Overlapped : %.3f ms\n", ovl);
        std::printf("  Speedup    : %.3fx (>1 needs a discrete GPU w/ copy engines)\n", speedup);

        EXPECT_TRUE(ok)  << "Sequential transfer/compute produced a wrong result.";
        EXPECT_TRUE(ok2) << "Overlapped transfer/compute produced a wrong result.";
        EXPECT_GT(speedup, 0.0);

        sycl::free(host_in, qa);
        sycl::free(host_out, qa);
        sycl::free(d, qa);
    }
} // namespace GraphCompileTests

/** @brief Measures the per-task cost of the work-stealing scheduler against an inline loop. */
namespace SchedulerDispatchTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
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

                const char* name() const noexcept override
                {
                    return "CountingTask";
                }

                ExecutionMode execution_mode() const override
                {
                    return ExecutionMode::Host;
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
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
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
            workers = sched.worker_count();
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
} // namespace SchedulerDispatchTests

/** @brief Tests the task scheduler under a task flood, producer contention and rapid restarts. */
namespace SchedulerStressTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using SushiRuntime::Test::AtomicCounterTask;
    using SushiRuntime::Test::PriorityTrackingTask;

    /**
     * @brief Tests scheduler stability and deque resizing under heavy load.
     * 
     * Submits one million tasks at once.
     */
    TEST(Stress_TaskSchedulerStress, OneMillionTaskBomb)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed_count{0};
        constexpr size_t TOTAL_TASKS = 1'000'000;

        scheduler.start();
        scheduler.arm_for_execution();

        for (size_t i = 0; i < TOTAL_TASKS; ++i)
            scheduler.submit(new AtomicCounterTask(executed_count));

        auto start_time = std::chrono::steady_clock::now();
        while (executed_count.load(std::memory_order_acquire) < TOTAL_TASKS)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30)
                FAIL() << "Scheduler failed to process 1 million tasks within 30 seconds!";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        scheduler.stop();
        EXPECT_EQ(executed_count.load(), TOTAL_TASKS);
    }

    /**
     * @brief Tests submission under contention.
     * 
     * Uses 8 producer threads submitting at once.
     */
    TEST(Stress_TaskSchedulerStress, MultiProducerContention)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed_count{0};
        constexpr int    NUM_PRODUCERS      = 8;
        constexpr size_t TASKS_PER_PRODUCER = 50'000;

        scheduler.start();
        scheduler.arm_for_execution();

        std::vector<std::thread> producers;
        for (int p = 0; p < NUM_PRODUCERS; ++p)
        {
            producers.emplace_back([&scheduler, &executed_count]()
            {
                for (size_t i = 0; i < TASKS_PER_PRODUCER; ++i)
                    scheduler.submit(new AtomicCounterTask(executed_count));
            });
        }

        for (auto& t : producers)
            if (t.joinable())
                t.join();

        auto start_time = std::chrono::steady_clock::now();
        while (executed_count.load(std::memory_order_acquire) < (NUM_PRODUCERS * TASKS_PER_PRODUCER))
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 30)
                FAIL() << "Multi-producer tasks were not processed within the time limit.";
            std::this_thread::yield();
        }

        scheduler.stop();
        EXPECT_EQ(executed_count.load(), NUM_PRODUCERS * TASKS_PER_PRODUCER);
    }

    /**
     * @brief Tests repeatedly arming and stopping the scheduler.
     * 
     * Helps to shake out races, deadlocks, and leaks.
     */
    TEST(Stress_TaskSchedulerStress, RapidArmAndStopThrashing)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        for (int i = 0; i < 500; ++i)
        {
            TaskScheduler scheduler = make_scheduler(ctx);
            std::atomic<size_t> executed_count{0};

            scheduler.start();
            scheduler.arm_for_execution();

            for (int j = 0; j < 1000; ++j)
                scheduler.submit(new AtomicCounterTask(executed_count));

            // Stop abruptly without waiting — exercises the drain in stop().
            scheduler.stop();
        }

        SUCCEED();
    }

    /**
     * @brief Tests high-priority task bypassing.
     * 
     * A high-priority task must bypass the queue even behind many low-priority ones.
     */
    TEST(Stress_TaskSchedulerStress, PriorityBypassUnderLoad)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::vector<int> execution_order;
        std::mutex order_mutex;

        scheduler.start();
        // Do NOT arm yet, so tasks stay in the injector.

        for (int i = 0; i < 50; ++i)
            scheduler.submit(new PriorityTrackingTask(0, execution_order, order_mutex));

        scheduler.submit(new PriorityTrackingTask(100, execution_order, order_mutex));

        scheduler.arm_for_execution();

        auto start_time = std::chrono::steady_clock::now();
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(order_mutex);
                if (execution_order.size() >= 51)
                    break;
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() > 5)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        scheduler.stop();

        ASSERT_EQ(execution_order.size(), 51);

        bool found_high_early = false;
        int check_window = static_cast<int>(scheduler.worker_count()) * 2;
        if (check_window > 51)
            check_window = 51;

        for (int i = 0; i < check_window; ++i)
        {
            if (execution_order[i] == 100)
            {
                found_high_early = true;
                break;
            }
        }

        EXPECT_TRUE(found_high_early) << "High priority task was not executed in the first batch!";
    }
} // namespace SchedulerStressTests

/** @brief Measures scheduler throughput and the local-to-remote steal split on multi-NUMA hardware. */
namespace StealLocalityTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using SushiRuntime::Test::AtomicCounterTask;

    namespace
    {
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
         * @brief Count the number of distinct NUMA nodes.
         * 
         * @param core_to_numa A mapping of CPU cores to NUMA node indices.
         * @return The number of distinct NUMA nodes.
         */
        int distinct_numa_nodes(const std::vector<int>& core_to_numa)
        {
            std::set<int> nodes(core_to_numa.begin(), core_to_numa.end());
            return static_cast<int>(nodes.size());
        }
    }

    /**
     * @brief Tests steal locality on multi-NUMA hardware.
     * 
     * Under heavy load on real multi-NUMA hardware, most steals must be node-local.
     */
    TEST(Performance_StealLocalityBench, StealStaysNodeLocal)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        const int nodes = distinct_numa_nodes(ctx.topology().core_to_numa());
        if (nodes < 2)
            GTEST_SKIP() << "Single NUMA node (" << nodes << ") — steal locality is "
                            "unmeasurable here; runs only on bare-metal multi-socket.";

        TaskScheduler sched = make_scheduler(ctx);

        std::atomic<size_t> executed{0};
        constexpr size_t TOTAL = 300000;

        sched.start();
        sched.arm_for_execution();

        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < TOTAL; ++i)
            sched.submit(new AtomicCounterTask(executed));

        ASSERT_TRUE(wait_for(executed, TOTAL, std::chrono::seconds(30)))
            << "Scheduler did not drain the steal-locality workload in time.";
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

        sched.stop();

        const size_t local      = sched.local_steal_count();
        const size_t remote     = sched.remote_steal_count();
        const size_t steals     = local + remote;
        const double local_frac = steals ? double(local) / double(steals) : 1.0;
        const double thr        = ms > 0 ? (double(TOTAL) / (ms / 1000.0)) : 0.0;

        std::printf("\n=== Steal locality (%d NUMA nodes, %zu tasks) ===\n", nodes, TOTAL);
        std::printf("  Throughput   : %10.0f tasks/s  (%.2f ms)\n", thr, ms);
        std::printf("  Local steals : %zu\n", local);
        std::printf("  Remote steals: %zu\n", remote);
        std::printf("  Local frac   : %.3f\n", local_frac);

        EXPECT_EQ(executed.load(), TOTAL);

        // Locality is asserted only above 1000 steals; a balanced drain produces few.
        if (steals > 1000)
            EXPECT_GT(local_frac, 0.5)
                << "Most steals crossed NUMA nodes — hierarchical locality is not holding.";
    }
} // namespace StealLocalityTests

/** @brief Measures stealing throughput on the real topology against a synthetic two-node one. */
namespace StealNumaEffectTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using namespace SushiRuntime::Internal::Topology;
    using SushiRuntime::Test::AtomicCounterTask;

    namespace
    {
        constexpr size_t K_TOTAL  = 300000;
        constexpr int    K_ROUNDS = 3;

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
         * @brief Creates a synthetic runtime context with a specified number of NUMA nodes.
         * 
         * @param nodes The number of synthetic NUMA nodes.
         * @return A shared pointer to the synthetic runtime context.
         */
        std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
        {
            auto provider = std::make_shared<RemappingTopologyProvider>(
                std::make_shared<HwlocTopologyProvider>(), nodes);
            std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
            for (int i = 0; i < nodes; ++i)
                matrix[i][i] = 10;
            auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));
            return SushiRuntime::Test::make_context({}, provider, distance);
        }

        /**
         * @brief Benchmark results containing execution time and steal counts.
         */
        struct StealMeasurement
        {
            double ms = 1e300;
            size_t local = 0;
            size_t remote = 0;
        };

        /**
         * @brief Drains K_TOTAL counter tasks through the scheduler of @p ctx, best of K_ROUNDS.
         * @return The fastest round's time and its steal split.
         */
        StealMeasurement run_workload(RuntimeContext& ctx)
        {
            StealMeasurement best;
            std::atomic<size_t> executed{0};
            for (int r = 0; r < K_ROUNDS; ++r)
            {
                TaskScheduler sched = make_scheduler(ctx);
                executed.store(0, std::memory_order_relaxed);
                sched.start();
                sched.arm_for_execution();

                const auto t0 = std::chrono::steady_clock::now();
                for (size_t i = 0; i < K_TOTAL; ++i)
                    sched.submit(new AtomicCounterTask(executed));
                EXPECT_TRUE(wait_for(executed, K_TOTAL, std::chrono::seconds(60)));
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();

                sched.stop();
                EXPECT_EQ(executed.load(), K_TOTAL); // consume

                if (ms < best.ms)
                    best = StealMeasurement{ ms, sched.local_steal_count(),
                                       sched.remote_steal_count() };
            }
            return best;
        }
    }

    /**
     * @brief Tests NUMA-aware vs NUMA-blind stealing throughput.
     * 
     * Same workload, same cores: report the throughput delta between the flat and
     * the synthetic-NUMA-aware scheduler, plus the aware steal locality.
     */
    TEST(Performance_StealNumaEffectBench, NUMAAwareVsBlindThroughput)
    {
        const auto blind_ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& blind_ctx = *blind_ctx_owner;
        const int workers = static_cast<int>(make_scheduler(blind_ctx).worker_count());
        if (workers < 2)
            GTEST_SKIP() << "Need >= 2 worker threads to exercise stealing.";

        auto aware_ctx = make_synthetic_ctx(2);

        const StealMeasurement blind = run_workload(blind_ctx);
        const StealMeasurement aware = run_workload(*aware_ctx);

        const double blind_thr = double(K_TOTAL) / (blind.ms / 1000.0);
        const double aware_thr = double(K_TOTAL) / (aware.ms / 1000.0);
        const double speedup   = blind.ms / aware.ms;
        const size_t a_steals  = aware.local + aware.remote;
        const double a_localf  = a_steals ? double(aware.local) / double(a_steals) : 1.0;

        std::printf("\n=== NUMA-aware vs blind stealing (%zu tasks, %d workers) ===\n",
                    K_TOTAL, workers);
        std::printf("  NUMA-blind : %10.0f tasks/s  (%.2f ms)\n", blind_thr, blind.ms);
        std::printf("  NUMA-aware : %10.0f tasks/s  (%.2f ms)\n", aware_thr, aware.ms);
        std::printf("  Speedup    : %.3fx (>1 only expected on real multi-socket)\n", speedup);
        std::printf("  Aware steal split: local=%zu remote=%zu (local frac %.3f)\n",
                    aware.local, aware.remote, a_localf);

        // No speedup assertion — on a UMA box there is no real locality benefit.
        EXPECT_GT(blind_thr, 0.0);
        EXPECT_GT(aware_thr, 0.0);

        // The split shows locality only across real NUMA nodes; see README.md.
        const auto& core_numa = blind_ctx.topology().core_to_numa();
        const std::set<int> real_nodes(core_numa.begin(), core_numa.end());
        if (real_nodes.size() < 2)
            GTEST_SKIP() << "Single physical NUMA node — synthetic-NUMA steal locality "
                            "is unmeasurable via timing here; covered deterministically "
                            "by Medium_NumaStealTest.";

        if (a_steals > 1000)
            EXPECT_GT(a_localf, 0.5)
                << "Synthetic-NUMA stealing crossed nodes too often — locality broken.";
    }
} // namespace StealNumaEffectTests

/** @brief Measures per-step drain latency and per-task cost over many small scheduler steps. */
namespace WideFanoutTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
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

                const char* name() const noexcept override
                {
                    return "CountingTask";
                }

                ExecutionMode execution_mode() const override
                {
                    return ExecutionMode::Host;
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

        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        std::atomic<std::size_t> counter{0};

        // The signal and the tasks are declared before the scheduler, so they outlive its workers.
        CompletionSignal drained;
        std::vector<sushi_ptr<CountingTask>> tasks;
        tasks.reserve(K_TASKS_PER_STEP);
        for (std::size_t k = 0; k < K_TASKS_PER_STEP; ++k)
            tasks.push_back(
                SushiRuntime::make_sushi<CountingTask>(counter, K_TASKS_PER_STEP, drained));

        TaskScheduler sched = make_scheduler(ctx);
        const std::size_t workers = sched.worker_count();

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
} // namespace WideFanoutTests
