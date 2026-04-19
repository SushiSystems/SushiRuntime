/**************************************************************************/
/* test_performance.cpp                                                   */
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

#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/task.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Memory;
using namespace SushiRuntime::Scheduler;

namespace
{
    using Clock = std::chrono::steady_clock;

    struct SampleStats
    {
        std::vector<double> values;
        double mean = 0.0;
        double median = 0.0;
        double p95 = 0.0;
        double min = 0.0;
        double max = 0.0;
        double stdev = 0.0;
    };

    template <typename T>
    inline void do_not_optimize(const T& value)
    {
    #if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : "g"(value) : "memory");
    #else
        std::atomic_signal_fence(std::memory_order_seq_cst);
        (void)value;
    #endif
    }

    inline void clobber_memory()
    {
    #if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : : "memory");
    #else
        std::atomic_signal_fence(std::memory_order_seq_cst);
    #endif
    }

    double ns_from_duration(const Clock::duration& d)
    {
        return std::chrono::duration<double, std::nano>(d).count();
    }

    SampleStats compute_stats(const std::vector<double>& samples)
    {
        SampleStats s;
        s.values = samples;

        if (samples.empty())
            return s;

        s.min = *std::min_element(samples.begin(), samples.end());
        s.max = *std::max_element(samples.begin(), samples.end());

        const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        s.mean = sum / static_cast<double>(samples.size());

        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());

        const size_t n = sorted.size();
        s.median = (n % 2 == 0)
            ? (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5
            : sorted[n / 2];

        const double idx = std::clamp(0.95 * static_cast<double>(n - 1), 0.0, static_cast<double>(n - 1));
        const size_t lower = static_cast<size_t>(idx);
        const size_t upper = std::min(lower + 1, n - 1);
        const double frac = idx - static_cast<double>(lower);
        s.p95 = sorted[lower] * (1.0 - frac) + sorted[upper] * frac;

        double variance = 0.0;
        for (double v : samples)
        {
            const double diff = v - s.mean;
            variance += diff * diff;
        }
        variance /= static_cast<double>(samples.size());
        s.stdev = std::sqrt(variance);

        return s;
    }

    template <typename Fn>
    SampleStats run_samples(int warmup_runs, int measured_runs, Fn&& fn)
    {
        for (int i = 0; i < warmup_runs; ++i)
            fn();

        std::vector<double> samples;
        samples.reserve(static_cast<size_t>(measured_runs));

        for (int i = 0; i < measured_runs; ++i)
        {
            const auto begin = Clock::now();
            fn();
            const auto end = Clock::now();
            samples.push_back(ns_from_duration(end - begin));
        }

        return compute_stats(samples);
    }

    void print_ops_summary(const std::string& name, size_t operations, const SampleStats& stats)
    {
        const double ops_per_sec = (stats.mean > 0.0)
            ? (static_cast<double>(operations) * 1e9 / stats.mean)
            : 0.0;
        const double ns_per_op = (operations > 0)
            ? (stats.mean / static_cast<double>(operations))
            : 0.0;

        std::cout << "[ PERF STAT ] " << std::left << std::setw(30) << name
                  << " | " << std::right << std::setw(15) << std::fixed << std::setprecision(2) << ops_per_sec << " Ops/sec"
                  << " | " << std::setw(10) << ns_per_op << " ns/Op"
                  << " | median " << std::setw(10) << stats.median
                  << " ns | p95 " << std::setw(10) << stats.p95
                  << " ns | stdev " << std::setw(10) << stats.stdev
                  << " ns\n";
    }

    void print_block()
    {
        std::cout << "------------------------------------------------------------\n";
    }

    class NoopTask final : public Task
    {
    public:
        explicit NoopTask(std::atomic<size_t>& executed)
            : Task("NoopTask"), executed_(executed) {}

        ExecutionMode get_execution_mode() const override { return ExecutionMode::HOST; }
        std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
        bool is_splittable() const override { return false; }

        sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
        {
            executed_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

    private:
        std::atomic<size_t>& executed_;
    };

    struct AllocatorBenchConfig
    {
        int warmup_runs = 2;
        int measured_runs = 10;
        size_t iterations = 1'000'000;
        size_t alloc_size = 256;
        bool touch_memory = true;
        int threads = 1;
    };

    template <typename AllocateFn, typename DeallocateFn>
    void run_allocator_loop(size_t iterations, size_t alloc_size, bool touch_memory, AllocateFn&& allocate, DeallocateFn&& deallocate)
    {
        for (size_t i = 0; i < iterations; ++i)
        {
            auto* ptr = static_cast<std::uint8_t*>(allocate(alloc_size));
            do_not_optimize(ptr);

            if (touch_memory && ptr != nullptr)
            {
                ptr[0] = static_cast<std::uint8_t>(i);
                ptr[alloc_size - 1] = static_cast<std::uint8_t>(i >> 8);
                clobber_memory();
            }

            deallocate(ptr);
        }
    }

    template <typename AllocateFn, typename DeallocateFn>
    SampleStats benchmark_allocator_single_thread(const AllocatorBenchConfig& cfg, AllocateFn&& allocate, DeallocateFn&& deallocate)
    {
        return run_samples(cfg.warmup_runs, cfg.measured_runs, [&]()
        {
            run_allocator_loop(cfg.iterations, cfg.alloc_size, cfg.touch_memory, allocate, deallocate);
        });
    }

    template <typename AllocateFn, typename DeallocateFn>
    SampleStats benchmark_allocator_multi_thread(const AllocatorBenchConfig& cfg, AllocateFn&& allocate, DeallocateFn&& deallocate)
    {
        return run_samples(cfg.warmup_runs, cfg.measured_runs, [&]()
        {
            std::barrier start_gate(cfg.threads + 1);
            std::vector<std::thread> workers;
            workers.reserve(static_cast<size_t>(cfg.threads));

            for (int t = 0; t < cfg.threads; ++t)
            {
                workers.emplace_back([&]()
                {
                    start_gate.arrive_and_wait();
                    run_allocator_loop(cfg.iterations, cfg.alloc_size, cfg.touch_memory, allocate, deallocate);
                });
            }

            start_gate.arrive_and_wait();
            for (auto& th : workers)
                th.join();
        });
    }

    struct SchedulerBenchConfig
    {
        int warmup_runs = 1;
        int measured_runs = 5;
        size_t total_tasks = 200'000;
    };

    SampleStats benchmark_scheduler_end_to_end(const SchedulerBenchConfig& cfg)
    {
        return run_samples(cfg.warmup_runs, cfg.measured_runs, [&]()
        {
            RuntimeContext ctx;
            TaskScheduler scheduler(ctx);
            std::atomic<size_t> executed_count{0};

            std::vector<std::unique_ptr<Task>> tasks;
            tasks.reserve(cfg.total_tasks);
            for (size_t i = 0; i < cfg.total_tasks; ++i)
                tasks.push_back(std::make_unique<NoopTask>(executed_count));

            scheduler.start();
            scheduler.arm_for_execution();

            const auto begin_submit = Clock::now();
            for (size_t i = 0; i < cfg.total_tasks; ++i)
            {
                scheduler.submit(tasks[i].release());
            }

            while (executed_count.load(std::memory_order_acquire) < cfg.total_tasks)
                std::this_thread::yield();

            const auto end_exec = Clock::now();
            scheduler.stop();

            do_not_optimize(begin_submit);
            do_not_optimize(end_exec);
        });
    }

    void print_allocator_header(const std::string& title)
    {
        print_block();
        std::cout << title << '\n';
        print_block();
    }
}

TEST(PerformanceBenchmark, Allocator_SingleThread_Stable)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    AllocatorBenchConfig cfg;
    cfg.warmup_runs = 2;
    cfg.measured_runs = 10;
    cfg.iterations = 1'000'000;
    cfg.alloc_size = 256;
    cfg.touch_memory = true;
    cfg.threads = 1;

    auto std_malloc_stats = benchmark_allocator_single_thread(
        cfg,
        [](size_t size) { return std::malloc(size); },
        [](void* ptr) { std::free(ptr); }
    );

    auto usm_stats = benchmark_allocator_single_thread(
        cfg,
        [&](size_t size) { return allocator->allocate(size); },
        [&](void* ptr) { allocator->deallocate(ptr); }
    );

    print_allocator_header("[ ALLOCATOR ] Single-thread, alloc+touch+free");
    print_ops_summary("std::malloc", cfg.iterations, std_malloc_stats);
    print_ops_summary("USM Allocator", cfg.iterations, usm_stats);
    print_block();

    SUCCEED();
}

TEST(PerformanceBenchmark, Allocator_MultiThread_Stable)
{
    RuntimeContext ctx;
    auto allocator = ctx.get_allocator();

    AllocatorBenchConfig cfg;
    cfg.warmup_runs = 2;
    cfg.measured_runs = 10;
    cfg.iterations = 250'000;
    cfg.alloc_size = 1024;
    cfg.touch_memory = true;
    cfg.threads = 12;

    auto std_malloc_stats = benchmark_allocator_multi_thread(
        cfg,
        [](size_t size) { return std::malloc(size); },
        [](void* ptr) { std::free(ptr); }
    );

    auto usm_stats = benchmark_allocator_multi_thread(
        cfg,
        [&](size_t size) { return allocator->allocate(size); },
        [&](void* ptr) { allocator->deallocate(ptr); }
    );

    print_allocator_header("[ ALLOCATOR ] Multi-thread, alloc+touch+free");
    print_ops_summary("std::malloc (12 Threads)", cfg.iterations * static_cast<size_t>(cfg.threads), std_malloc_stats);
    print_ops_summary("USM Allocator (12 Threads)", cfg.iterations * static_cast<size_t>(cfg.threads), usm_stats);
    print_block();

    SUCCEED();
}

TEST(PerformanceBenchmark, TaskScheduler_EndToEnd_Stable)
{
    SchedulerBenchConfig cfg;
    cfg.warmup_runs = 1;
    cfg.measured_runs = 5;
    cfg.total_tasks = 200'000;

    auto stats = benchmark_scheduler_end_to_end(cfg);

    print_block();
    print_ops_summary("Task Scheduler (End-to-End)", cfg.total_tasks, stats);
    print_block();

    EXPECT_LT(stats.mean / 1e9, 15.0) << "Scheduler benchmark is too slow.";
}
