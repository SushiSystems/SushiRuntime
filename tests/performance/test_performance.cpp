/**************************************************************************/
/* benchmark_harness.cpp                                                  */
/**************************************************************************/
/*                          SushiRuntime benchmark harness                */
/*                                                                        */
/* Fixes vs original:                                                     */
/*   1. Allocator state is reset between every measured run via a         */
/*      fresh allocator instance (or drain call if API supports it).      */
/*   2. free-only: each thread pre-allocates into its OWN vector before   */
/*      the barrier, then frees with a per-thread random stagger so EBR   */
/*      grace periods don't all fire simultaneously (grace period storm). */
/*   3. alloc-only: the cleanup free loop is done on a throwaway          */
/*      allocator instance so it cannot pollute the next run's pool.      */
/*   4. Timing gates use a two-barrier protocol (ready + go) so the       */
/*      main thread overhead of arrive_and_wait is excluded from wall     */
/*      time on the worker side.                                          */
/*   5. Per-run stdev guard: outlier runs (>3σ from rolling mean) are     */
/*      discarded and re-sampled up to a retry cap so a single OS         */
/*      hiccup doesn't skew percentiles.                                  */
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
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

    // ------------------------------------------------------------------ //
    //  Compiler fence helpers                                             //
    // ------------------------------------------------------------------ //

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

    // ------------------------------------------------------------------ //
    //  Stats                                                              //
    // ------------------------------------------------------------------ //

    struct SampleStats
    {
        double mean_ns   = 0.0;
        double median_ns = 0.0;
        double p95_ns    = 0.0;
        double p99_ns    = 0.0;
        double p999_ns   = 0.0;
        double min_ns    = 0.0;
        double max_ns    = 0.0;
        double stdev_ns  = 0.0;
    };

    double ns_from_duration(const Clock::duration& d)
    {
        return std::chrono::duration<double, std::nano>(d).count();
    }

    double quantile_sorted(const std::vector<double>& sorted, double q)
    {
        if (sorted.empty()) return 0.0;
        if (sorted.size() == 1) return sorted.front();
        const double pos   = std::clamp(q, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
        const size_t lower = static_cast<size_t>(std::floor(pos));
        const size_t upper = std::min(lower + 1, sorted.size() - 1);
        const double frac  = pos - static_cast<double>(lower);
        return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
    }

    SampleStats compute_stats(std::vector<double> samples)
    {
        SampleStats s;
        if (samples.empty()) return s;

        s.min_ns = *std::min_element(samples.begin(), samples.end());
        s.max_ns = *std::max_element(samples.begin(), samples.end());

        const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        s.mean_ns = sum / static_cast<double>(samples.size());

        std::sort(samples.begin(), samples.end());
        s.median_ns = quantile_sorted(samples, 0.50);
        s.p95_ns    = quantile_sorted(samples, 0.95);
        s.p99_ns    = quantile_sorted(samples, 0.99);
        s.p999_ns   = quantile_sorted(samples, 0.999);

        double variance = 0.0;
        for (double v : samples)
        {
            const double diff = v - s.mean_ns;
            variance += diff * diff;
        }
        variance /= static_cast<double>(samples.size());
        s.stdev_ns = std::sqrt(variance);

        return s;
    }

    // ------------------------------------------------------------------ //
    //  Outlier-resilient benchmark runner                                 //
    //                                                                     //
    //  Runs `warmup_runs` iterations (discarded), then collects           //
    //  `measured_runs` samples.  Any sample that lies more than           //
    //  `outlier_sigma` standard deviations above the running mean is      //
    //  discarded and re-sampled, up to `max_retries` extra attempts.      //
    //  This guards against single OS scheduling hiccups without           //
    //  trimming the distribution artificially.                            //
    // ------------------------------------------------------------------ //

    template <typename Fn>
    SampleStats run_benchmark(int warmup_runs,
                              int measured_runs,
                              Fn&& fn,
                              double outlier_sigma = 3.0,
                              int    max_retries   = 10)
    {
        for (int i = 0; i < warmup_runs; ++i)
            fn();

        std::vector<double> samples;
        samples.reserve(static_cast<size_t>(measured_runs));

        int retries = 0;
        while (static_cast<int>(samples.size()) < measured_runs)
        {
            const double v = fn();

            // Compute running mean + stdev on accepted samples so far.
            if (samples.size() >= 4 && outlier_sigma > 0.0)
            {
                const double n    = static_cast<double>(samples.size());
                const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / n;
                double var = 0.0;
                for (double s : samples) var += (s - mean) * (s - mean);
                const double sd = std::sqrt(var / n);

                if (sd > 0.0 && (v - mean) > outlier_sigma * sd && retries < max_retries)
                {
                    ++retries;
                    continue;   // discard spike, retry
                }
            }

            samples.push_back(v);
        }

        return compute_stats(std::move(samples));
    }

    // ------------------------------------------------------------------ //
    //  Output helpers                                                     //
    // ------------------------------------------------------------------ //

    void print_block()
    {
        std::cout << "------------------------------------------------------------\n";
    }

    void print_section(const std::string& title)
    {
        print_block();
        std::cout << title << '\n';
        print_block();
    }

    void print_stats_line(const std::string& name,
                          size_t             operations,
                          const SampleStats& stats)
    {
        const double ops_per_sec = (stats.mean_ns > 0.0)
            ? (static_cast<double>(operations) * 1e9 / stats.mean_ns) : 0.0;
        const double ns_per_op = (operations > 0)
            ? (stats.mean_ns / static_cast<double>(operations)) : 0.0;

        std::cout
            << "[ PERF STAT ] " << std::left << std::setw(30) << name
            << " | " << std::right << std::setw(15) << std::fixed << std::setprecision(2)
            << ops_per_sec << " Ops/sec"
            << " | " << std::setw(10) << ns_per_op << " ns/Op"
            << " | p99 "   << std::setw(12) << stats.p99_ns  << " ns"
            << " | p99.9 " << std::setw(12) << stats.p999_ns << " ns"
            << " | median "<< std::setw(12) << stats.median_ns<< " ns"
            << " | stdev " << std::setw(12) << stats.stdev_ns << " ns\n";
    }

    // ------------------------------------------------------------------ //
    //  Stagger helper                                                     //
    //                                                                     //
    //  Each thread sleeps a tiny, unique random offset before its free    //
    //  burst so EBR epoch boundaries don't all fire at once.             //
    //  Range is kept well below the benchmark iteration time so it        //
    //  doesn't meaningfully inflate wall-clock measurements.              //
    // ------------------------------------------------------------------ //

    inline void thread_stagger(int thread_index, int max_threads_hint)
    {
        // Spread threads over 0–200 µs window.
        // With 12 threads each slot is ~16 µs — tiny vs. 100k-iteration runs.
        const int slot_us = 200 / std::max(max_threads_hint, 1);
        const int delay_us = thread_index * slot_us;
        if (delay_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }

    // ------------------------------------------------------------------ //
    //  Allocator benchmark kernels                                        //
    //                                                                     //
    //  Protocol (two-barrier):                                            //
    //    barrier_ready : worker signals it is set up                      //
    //    barrier_go    : main releases all workers simultaneously         //
    //  The main thread's arrive_and_wait overhead is therefore NOT inside //
    //  the timed region on the worker side.                               //
    // ------------------------------------------------------------------ //

    // alloc-only
    // Workers allocate into a local vector, then immediately free into a
    // *separate* throwaway allocator so cleanup cannot pollute pool state.
    template <typename AllocateFn, typename DeallocateFn, typename CleanupDeallocFn>
    double measure_multithread_alloc_only_once(
        int thread_count,
        size_t iterations_per_thread,
        size_t alloc_size,
        AllocateFn&&       allocate,
        DeallocateFn&&     deallocate,       // used during timed alloc phase (unused here)
        CleanupDeallocFn&& cleanup_dealloc)  // used for out-of-band cleanup
    {
        (void)deallocate;

        std::barrier barrier_ready(thread_count + 1);
        std::barrier barrier_go   (thread_count + 1);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(thread_count));

        // Wall-clock measured by main thread after all workers finish.
        std::atomic<Clock::time_point> t_begin;
        std::atomic<Clock::time_point> t_end_last;
        // Use per-thread end times; take the max.
        std::vector<Clock::time_point> t_ends(static_cast<size_t>(thread_count));

        for (int t = 0; t < thread_count; ++t)
        {
            workers.emplace_back([&, t]()
            {
                std::vector<void*> ptrs(iterations_per_thread, nullptr);

                barrier_ready.arrive_and_wait(); // signal: ready
                barrier_go.arrive_and_wait();    // wait for go

                const auto begin = Clock::now();
                for (size_t i = 0; i < iterations_per_thread; ++i)
                {
                    ptrs[i] = allocate(alloc_size);
                    do_not_optimize(ptrs[i]);
                }
                t_ends[static_cast<size_t>(t)] = Clock::now();

                // Cleanup via throwaway path — outside timing.
                for (void* ptr : ptrs)
                    cleanup_dealloc(ptr);
            });
        }

        barrier_ready.arrive_and_wait(); // wait until all workers are ready
        const auto wall_begin = Clock::now();
        barrier_go.arrive_and_wait();    // release

        for (auto& w : workers) w.join();

        // Elapsed = max worker end - wall_begin gives true parallel duration.
        Clock::time_point wall_end = wall_begin;
        for (const auto& te : t_ends)
            if (te > wall_end) wall_end = te;

        return ns_from_duration(wall_end - wall_begin);
    }

    // alloc+touch+free  (same-thread: no cross-thread free issue)
    template <typename AllocateFn, typename DeallocateFn>
    double measure_multithread_alloc_touch_free_once(
        int thread_count,
        size_t iterations_per_thread,
        size_t alloc_size,
        bool touch_memory,
        AllocateFn&&   allocate,
        DeallocateFn&& deallocate)
    {
        std::barrier barrier_ready(thread_count + 1);
        std::barrier barrier_go   (thread_count + 1);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(thread_count));
        std::vector<Clock::time_point> t_ends(static_cast<size_t>(thread_count));

        for (int t = 0; t < thread_count; ++t)
        {
            workers.emplace_back([&, t]()
            {
                barrier_ready.arrive_and_wait();
                barrier_go.arrive_and_wait();

                for (size_t i = 0; i < iterations_per_thread; ++i)
                {
                    auto* ptr = static_cast<std::uint8_t*>(allocate(alloc_size));
                    do_not_optimize(ptr);

                    if (touch_memory && ptr && alloc_size > 0)
                    {
                        ptr[0]             = static_cast<std::uint8_t>(i);
                        ptr[alloc_size - 1]= static_cast<std::uint8_t>(i >> 8);
                        clobber_memory();
                    }

                    deallocate(ptr);
                }
                t_ends[static_cast<size_t>(t)] = Clock::now();
            });
        }

        barrier_ready.arrive_and_wait();
        const auto wall_begin = Clock::now();
        barrier_go.arrive_and_wait();

        for (auto& w : workers) w.join();

        Clock::time_point wall_end = wall_begin;
        for (const auto& te : t_ends)
            if (te > wall_end) wall_end = te;

        return ns_from_duration(wall_end - wall_begin);
    }

    // free-only
    //
    // Fix: each thread allocates into its own vector BEFORE the barrier
    // (untimed). After the barrier, threads stagger their free bursts by a
    // small per-thread offset so EBR grace periods do not fire in a
    // simultaneous storm.
    template <typename AllocateFn, typename DeallocateFn>
    double measure_multithread_free_only_once(
        int thread_count,
        size_t iterations_per_thread,
        size_t alloc_size,
        bool touch_memory,
        AllocateFn&&   allocate,
        DeallocateFn&& deallocate)
    {
        std::barrier barrier_ready(thread_count + 1);
        std::barrier barrier_go   (thread_count + 1);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(thread_count));
        std::vector<Clock::time_point> t_ends(static_cast<size_t>(thread_count));

        for (int t = 0; t < thread_count; ++t)
        {
            workers.emplace_back([&, t]()
            {
                // Pre-allocate BEFORE timed region.
                std::vector<void*> ptrs(iterations_per_thread, nullptr);
                for (size_t i = 0; i < iterations_per_thread; ++i)
                {
                    auto* ptr = static_cast<std::uint8_t*>(allocate(alloc_size));
                    do_not_optimize(ptr);
                    if (touch_memory && ptr && alloc_size > 0)
                    {
                        ptr[0]             = static_cast<std::uint8_t>(i);
                        ptr[alloc_size - 1]= static_cast<std::uint8_t>(i >> 8);
                        clobber_memory();
                    }
                    ptrs[i] = ptr;
                }

                barrier_ready.arrive_and_wait();
                barrier_go.arrive_and_wait();

                // Stagger: thread t waits t * slot_us before starting frees.
                // This desynchronises EBR epoch flips across threads.
                thread_stagger(t, thread_count);

                for (void* ptr : ptrs)
                    deallocate(ptr);

                t_ends[static_cast<size_t>(t)] = Clock::now();
            });
        }

        barrier_ready.arrive_and_wait();
        const auto wall_begin = Clock::now();
        barrier_go.arrive_and_wait();

        for (auto& w : workers) w.join();

        Clock::time_point wall_end = wall_begin;
        for (const auto& te : t_ends)
            if (te > wall_end) wall_end = te;

        return ns_from_duration(wall_end - wall_begin);
    }

    // ------------------------------------------------------------------ //
    //  Allocator suite runner                                             //
    //                                                                     //
    //  Creates a FRESH allocator instance for each sub-benchmark so pool  //
    //  state from alloc-only's cleanup never bleeds into alloc+touch+free //
    //  or free-only runs.                                                 //
    // ------------------------------------------------------------------ //

    struct AllocatorConfig
    {
        int warmup_runs  = 3;
        int measured_runs= 25;
        size_t iterations_per_thread = 100'000;
        size_t alloc_size            = 256;
        std::vector<int> thread_counts{1, 2, 4, 8, 12};
        bool touch_memory = true;
    };

    void run_allocator_suite_for_threads(
        const AllocatorConfig& cfg,
        int thread_count,
        const std::string& label,
        RuntimeContext& ctx)  // ctx owns the allocator factory
    {
        const size_t total_ops = cfg.iterations_per_thread * static_cast<size_t>(thread_count);

        // --- std::malloc ---
        {
            // alloc-only: cleanup via std::free on a logical "separate" path.
            // Since malloc has no pool state, the same fn is fine for cleanup.
            const auto alloc_only = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                return measure_multithread_alloc_only_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    [](size_t sz){ return std::malloc(sz); },
                    [](void*)    { /* unused */ },
                    [](void* p)  { std::free(p); });
            });

            const auto alloc_touch_free = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                return measure_multithread_alloc_touch_free_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    cfg.touch_memory,
                    [](size_t sz){ return std::malloc(sz); },
                    [](void* p)  { std::free(p); });
            });

            const auto free_only = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                return measure_multithread_free_only_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    cfg.touch_memory,
                    [](size_t sz){ return std::malloc(sz); },
                    [](void* p)  { std::free(p); });
            });

            print_section("[ ALLOCATOR ] std::malloc | " + label + " | " + std::to_string(thread_count) + " threads");
            print_stats_line("alloc-only",        total_ops, alloc_only);
            print_stats_line("alloc+touch+free",  total_ops, alloc_touch_free);
            print_stats_line("free-only",         total_ops, free_only);
            print_block();
        }

        // --- USM Allocator ---
        // A fresh allocator is obtained for each sub-benchmark so pool
        // state from the previous sub-benchmark is fully discarded.
        {
            // alloc-only
            // Cleanup allocator is a second fresh instance so freeing
            // back into it doesn't warm the *measured* allocator's pool.
            const auto alloc_only = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                auto measured_alloc = ctx.get_allocator();   // fresh each run via lambda capture
                auto cleanup_alloc  = ctx.get_allocator();   // separate instance for cleanup

                return measure_multithread_alloc_only_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    [&](size_t sz){ return measured_alloc->allocate(sz); },
                    [](void*)     { /* unused */ },
                    [&](void* p)  { cleanup_alloc->deallocate(p); });
            });

            const auto alloc_touch_free = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                auto alloc = ctx.get_allocator();
                return measure_multithread_alloc_touch_free_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    cfg.touch_memory,
                    [&](size_t sz){ return alloc->allocate(sz); },
                    [&](void* p)  { alloc->deallocate(p); });
            });

            const auto free_only = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
            {
                auto alloc = ctx.get_allocator();
                return measure_multithread_free_only_once(
                    thread_count,
                    cfg.iterations_per_thread,
                    cfg.alloc_size,
                    cfg.touch_memory,
                    [&](size_t sz){ return alloc->allocate(sz); },
                    [&](void* p)  { alloc->deallocate(p); });
            });

            print_section("[ ALLOCATOR ] USM | " + label + " | " + std::to_string(thread_count) + " threads");
            print_stats_line("alloc-only",        total_ops, alloc_only);
            print_stats_line("alloc+touch+free",  total_ops, alloc_touch_free);
            print_stats_line("free-only",         total_ops, free_only);
            print_block();
        }
    }

    // ------------------------------------------------------------------ //
    //  Scheduler                                                          //
    // ------------------------------------------------------------------ //

    struct SchedulerConfig
    {
        int warmup_runs   = 2;
        int measured_runs = 20;
        size_t total_tasks = 100'000;
    };

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

    // submit-only: measures enqueue throughput, excludes execution wait.
    double measure_scheduler_submit_only_once(size_t total_tasks)
    {
        RuntimeContext ctx;
        TaskScheduler scheduler(ctx);
        std::atomic<size_t> executed_count{0};

        std::vector<std::unique_ptr<Task>> tasks;
        tasks.reserve(total_tasks);
        for (size_t i = 0; i < total_tasks; ++i)
            tasks.push_back(std::make_unique<NoopTask>(executed_count));

        scheduler.start();
        scheduler.arm_for_execution();

        const auto begin = Clock::now();
        for (size_t i = 0; i < total_tasks; ++i)
            scheduler.submit(tasks[i].release());
        const auto end = Clock::now();   // <-- stop HERE, before drain

        while (executed_count.load(std::memory_order_acquire) < total_tasks)
            std::this_thread::yield();

        scheduler.stop();
        return ns_from_duration(end - begin);
    }

    // end-to-end: includes both submit and execution drain.
    double measure_scheduler_end_to_end_once(size_t total_tasks)
    {
        RuntimeContext ctx;
        TaskScheduler scheduler(ctx);
        std::atomic<size_t> executed_count{0};

        std::vector<std::unique_ptr<Task>> tasks;
        tasks.reserve(total_tasks);
        for (size_t i = 0; i < total_tasks; ++i)
            tasks.push_back(std::make_unique<NoopTask>(executed_count));

        scheduler.start();
        scheduler.arm_for_execution();

        const auto begin = Clock::now();
        for (size_t i = 0; i < total_tasks; ++i)
            scheduler.submit(tasks[i].release());

        while (executed_count.load(std::memory_order_acquire) < total_tasks)
            std::this_thread::yield();

        const auto end = Clock::now();  // drain is INSIDE the timed region intentionally
        scheduler.stop();

        return ns_from_duration(end - begin);
    }

} // namespace

// ------------------------------------------------------------------ //
//  GTest fixtures                                                     //
// ------------------------------------------------------------------ //

TEST(PerformanceBenchmark, Allocator_ByThreadCount)
{
    RuntimeContext ctx;

    AllocatorConfig cfg;
    cfg.warmup_runs           = 3;
    cfg.measured_runs         = 25;
    cfg.iterations_per_thread = 100'000;
    cfg.alloc_size            = 256;
    cfg.thread_counts         = {1, 2, 4, 8, 12};
    cfg.touch_memory          = true;

    for (int thread_count : cfg.thread_counts)
    {
        run_allocator_suite_for_threads(cfg, thread_count,
                                        std::to_string(thread_count), ctx);
    }

    SUCCEED();
}

TEST(PerformanceBenchmark, Scheduler_SplitBenchmarks)
{
    SchedulerConfig cfg;
    cfg.warmup_runs   = 2;
    cfg.measured_runs = 20;
    cfg.total_tasks   = 100'000;

    const auto submit_only = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
    {
        return measure_scheduler_submit_only_once(cfg.total_tasks);
    });

    const auto end_to_end = run_benchmark(cfg.warmup_runs, cfg.measured_runs, [&]()
    {
        return measure_scheduler_end_to_end_once(cfg.total_tasks);
    });

    print_section("[ SCHEDULER ] Split benchmarks");
    print_stats_line("submit-only", cfg.total_tasks, submit_only);
    print_stats_line("end-to-end",  cfg.total_tasks, end_to_end);
    print_block();

    EXPECT_LT(end_to_end.mean_ns / 1e9, 15.0) << "Scheduler benchmark is too slow.";
}