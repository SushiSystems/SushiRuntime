/**************************************************************************/
/* test_allocator_perf.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Honest comparison: the SushiRuntime USM pool vs std::malloc (the non-USM host
// floor). Scenarios: same-thread alloc/free (magazine best case) and
// producer/consumer (cross-thread free, the realistic task-graph pattern), over
// several size classes. Batched timing feeds mean/p50/throughput; a separate
// rdtsc per-op probe feeds p99/p999/p9999 so tail spikes aren't diluted.

#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <cstdint>
#include <thread>
#include <vector>

#include "test_barrier.hpp"

#if defined(_MSC_VER)
#  include <intrin.h>
#  define SR_HAS_RDTSC 1
#elif defined(__x86_64__) || defined(__i386__)
#  include <x86intrin.h>
#  define SR_HAS_RDTSC 1
#endif

#include <sycl/sycl.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Memory;

namespace
{
    using Clock = std::chrono::steady_clock;

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

    /**
     * @brief Per-op timer backed by the CPU timestamp counter.
     * 
     * steady_clock can have a ~100ns tick on virtualized hosts — coarser than a single pool op — which
     * floors the per-op tail. rdtsc gives sub-ns resolution; only same-thread
     * deltas are taken, so cross-core TSC sync is irrelevant. Falls back to
     * steady_clock off x86.
     */
    struct HiResTimer
    {
        static inline double ns_per_tick = 1.0;
        // Cost of one empty ticks()..ticks() bracket; subtracted per sample so
        // the reported tail is allocator cost, not instrument cost.
        static inline double overhead_ns = 0.0;

        static inline uint64_t ticks()
        {
#if defined(SR_HAS_RDTSC)
            _mm_lfence();
            uint64_t t = __rdtsc();
            _mm_lfence();
            return t;
#else
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    Clock::now().time_since_epoch()).count());
#endif
        }

        /**
         * @brief Pin the tick rate against steady_clock over a busy interval.
         */
        static void calibrate()
        {
#if defined(SR_HAS_RDTSC)
            const auto window = std::chrono::milliseconds(100);
            uint64_t c0 = ticks();
            auto w0 = Clock::now();
            while (Clock::now() - w0 < window)
                do_not_optimize(c0);
            auto w1 = Clock::now();
            uint64_t c1 = ticks();
            double ns = std::chrono::duration<double, std::nano>(w1 - w0).count();
            ns_per_tick = ns / static_cast<double>(c1 - c0);

            // Minimum back-to-back bracket delta = pure instrument overhead.
            uint64_t min_ticks = UINT64_MAX;
            for (int i = 0; i < 4096; ++i)
            {
                uint64_t a = ticks();
                uint64_t b = ticks();
                if (b - a < min_ticks)
                    min_ticks = b - a;
            }
            overhead_ns = static_cast<double>(min_ticks) * ns_per_tick;
#else
            ns_per_tick = 1.0;
            overhead_ns = 0.0;
#endif
        }
    };

    /**
     * @brief Per-op latency in ns from a tick bracket, instrument overhead removed.
     * 
     * @param start The start tick.
     * @param end The end tick.
     * @return The latency in nanoseconds.
     */
    inline double op_ns(uint64_t start, uint64_t end)
    {
        double v = static_cast<double>(end - start) * HiResTimer::ns_per_tick
                   - HiResTimer::overhead_ns;
        return v < 0.0 ? 0.0 : v;
    }

    /**
     * @brief Touch every cache line so the real first-touch (page commit) cost is paid.
     * 
     * @param p Pointer to the memory.
     * @param size Size of the memory in bytes.
     */
    inline void touch_pages(void* p, size_t size)
    {
        if (!p || size == 0)
            return;
        auto* c = static_cast<volatile char*>(p);
        for (size_t i = 0; i < size; i += 64)
            c[i] = 0;
        c[size - 1] = 0;
    }

    /**
     * @brief Per-round latency statistics from a sorted sample vector.
     */
    struct LatencyDist
    {
        double p50 = 0, p99 = 0, p999 = 0, p9999 = 0, mean = 0;
        double ops_per_sec = 0;
    };

    /**
     * @brief Build a distribution from two sources.
     * 
     * block_means (batched) feeds mean, p50, throughput; per_op (single-op) feeds the tail.
     * Sorts both in place.
     * 
     * @param block_means Batched means.
     * @param per_op Single-op times.
     * @param total_ops Total operations.
     * @param total_ns Total nanoseconds.
     * @return Computed latency distribution.
     */
    LatencyDist compute_latency2(std::vector<double>& block_means,
                                 std::vector<double>& per_op,
                                 double total_ops, double total_ns)
    {
        LatencyDist d;
        if (block_means.empty())
            return d;

        std::sort(block_means.begin(), block_means.end());
        double sum = 0;
        for (double v : block_means)
            sum += v;
        d.mean = sum / block_means.size();
        d.p50  = block_means[static_cast<size_t>(0.50 * (block_means.size() - 1))];

        std::vector<double>& tail = per_op.empty() ? block_means : per_op;
        std::sort(tail.begin(), tail.end());
        auto pct = [&](double p)
        {
            return tail[static_cast<size_t>(p * (tail.size() - 1))];
        };
        d.p99   = pct(0.99);
        d.p999  = pct(0.999);
        d.p9999 = pct(0.9999); // more reliable than the raw max (single event)

        d.ops_per_sec = (total_ns > 0) ? (total_ops * 1e9 / total_ns) : 0;
        return d;
    }

    /**
     * @brief Per-round results for a producer/consumer contestant, kept split.
     */
    struct PCResult
    {
        LatencyDist alloc;   // producer side: alloc + first-touch + handoff
        LatencyDist free_;   // consumer side: free() only
    };

    /**
     * @brief Median across rounds, field by field — robust against a single outlier.
     * 
     * @param rs The latency distributions.
     * @return Aggregated latency distribution.
     */
    LatencyDist aggregate_rounds(std::vector<LatencyDist>& rs)
    {
        auto med = [&](double LatencyDist::*field)
        {
            std::vector<double> v;
            v.reserve(rs.size());
            for (auto& d : rs)
                v.push_back(d.*field);
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0 : v[v.size() / 2];
        };
        LatencyDist a;
        a.mean        = med(&LatencyDist::mean);
        a.p50         = med(&LatencyDist::p50);
        a.p99         = med(&LatencyDist::p99);
        a.p999        = med(&LatencyDist::p999);
        a.p9999       = med(&LatencyDist::p9999);
        a.ops_per_sec = med(&LatencyDist::ops_per_sec);
        return a;
    }

    /**
     * @brief Timing-agnostic sanity gate.
     * 
     * This is a benchmark, not a perf regression test, so it only asserts forward
     * progress (no deadlock / no absurd stall).
     * 
     * @param name Name of the test.
     * @param d The latency distribution.
     */
    void sanity_gate(const std::string& name, const LatencyDist& d)
    {
        EXPECT_GT(d.ops_per_sec, 0.0) << name << ": no forward progress (possible deadlock)";
        EXPECT_LT(d.mean, 1.0e7)      << name << ": mean latency absurd (>10ms), likely a stall";
    }

    void print_dist(const std::string& name, const LatencyDist& d)
    {
        std::cout << "  " << std::left << std::setw(28) << name
                  << " | mean " << std::right << std::setw(7) << std::fixed << std::setprecision(1) << d.mean << "ns"
                  << " | p50 "  << std::setw(7) << d.p50  << "ns"
                  << " | p99 "  << std::setw(7) << d.p99  << "ns"
                  << " | p999 "  << std::setw(8) << d.p999  << "ns"
                  << " | p9999 " << std::setw(8) << d.p9999 << "ns"
                  << " | "      << std::setw(11) << std::setprecision(0) << d.ops_per_sec << " ops/s\n";
    }

    /**
     * @brief Pluggable allocator interface so we can swap the contestant (pool, malloc).
     */
    struct AllocatorVTable
    {
        std::string name;
        std::function<void*(size_t)>  alloc;
        std::function<void(void*)>    free_;
    };

    /**
     * @brief Warm the upcoming scenario's size-class bucket before timing begins.
     * 
     * This prevents the first measured iterations from paying a cold-bucket refill spike.
     * 
     * @param av The allocator vtable.
     * @param alloc_size The allocation size.
     * @param iters Number of iterations.
     */
    void warmup_scenario(const AllocatorVTable& av, size_t alloc_size, size_t iters = 4096)
    {
        for (size_t i = 0; i < iters; ++i)
        {
            void* p = av.alloc(alloc_size);
            do_not_optimize(p);
            touch_pages(p, alloc_size);
            av.free_(p);
        }
    }

    /**
     * @brief Benchmark same-thread alloc/free (magazine best case).
     * 
     * Phase 1 times batches (mean/p50/throughput, tick amortized); phase 2 times each op
     * via rdtsc (p99/p999/p9999). Only phase 1 counts toward ops/s.
     * 
     * @param av The allocator vtable.
     * @param thread_count The number of threads.
     * @param iters_per_thread Iterations per thread.
     * @param alloc_size The allocation size.
     * @param batch The batch size.
     * @return Measured latency distribution.
     */
    LatencyDist bench_same_thread(const AllocatorVTable& av,
                                  size_t thread_count,
                                  size_t iters_per_thread,
                                  size_t alloc_size,
                                  size_t batch = 256)
    {
        const size_t tail_iters = std::min<size_t>(iters_per_thread, 50'000);

        std::vector<std::vector<double>> block_means(thread_count);
        std::vector<std::vector<double>> per_op(thread_count);
        SushiRuntime::Test::Barrier go(thread_count + 1);
        SushiRuntime::Test::Barrier mid(thread_count + 1);
        SushiRuntime::Test::Barrier done(thread_count + 1);
        std::vector<std::thread> workers;
        workers.reserve(thread_count);

        for (size_t t = 0; t < thread_count; ++t)
        {
            workers.emplace_back([&, t]
            {
                auto& bm = block_means[t];
                auto& po = per_op[t];
                bm.reserve(iters_per_thread / batch + 1);
                po.reserve(tail_iters);

                go.arrive_and_wait();

                // Phase 1: batched timing (mean / p50 / throughput).
                for (size_t i = 0; i < iters_per_thread; i += batch)
                {
                    size_t n = std::min(batch, iters_per_thread - i);
                    auto t0 = Clock::now();
                    for (size_t j = 0; j < n; ++j)
                    {
                        void* p = av.alloc(alloc_size);
                        do_not_optimize(p);
                        touch_pages(p, alloc_size);
                        av.free_(p);
                    }
                    auto t1 = Clock::now();
                    bm.push_back(
                        std::chrono::duration<double, std::nano>(t1 - t0).count()
                            / static_cast<double>(n));
                }

                mid.arrive_and_wait();

                // Phase 2: per-op timing (p99 / p999 / p9999) via rdtsc.
                for (size_t k = 0; k < tail_iters; ++k)
                {
                    uint64_t a = HiResTimer::ticks();
                    void* p = av.alloc(alloc_size);
                    do_not_optimize(p);
                    touch_pages(p, alloc_size);
                    av.free_(p);
                    uint64_t b = HiResTimer::ticks();
                    po.push_back(op_ns(a, b));
                }

                done.arrive_and_wait();
            });
        }

        auto round_start = Clock::now();
        go.arrive_and_wait();
        mid.arrive_and_wait();
        auto round_end = Clock::now(); // batched phase wall time only
        done.arrive_and_wait();

        for (auto& w : workers)
            w.join();

        std::vector<double> bm_all, po_all;
        for (auto& s : block_means)
            bm_all.insert(bm_all.end(), s.begin(), s.end());
        for (auto& s : per_op)
            po_all.insert(po_all.end(), s.begin(), s.end());

        double total_ns  = std::chrono::duration<double, std::nano>(round_end - round_start).count();
        double total_ops = static_cast<double>(thread_count * iters_per_thread);
        return compute_latency2(bm_all, po_all, total_ops, total_ns);
    }

    /**
     * @brief Benchmark producer/consumer alloc/free (cross-thread free).
     * 
     * Half the threads alloc + push into a bounded SPSC ring, the other half pop + free.
     * The two sides are timed and reported SEPARATELY so the remote-free cost is a first-class
     * number. Same two-phase (batched + rdtsc per-op) timing as above.
     * 
     * @param av The allocator vtable.
     * @param pair_count Number of producer/consumer pairs.
     * @param iters_per_pair Iterations per pair.
     * @param alloc_size The allocation size.
     * @param batch The batch size.
     * @return Producer/consumer results.
     */
    PCResult bench_producer_consumer(const AllocatorVTable& av,
                                     size_t pair_count,
                                     size_t iters_per_pair,
                                     size_t alloc_size,
                                     size_t batch = 256)
    {
        constexpr size_t RING = 1024;
        struct Ring
        {
            alignas(64) std::atomic<size_t> head{0};
            alignas(64) std::atomic<size_t> tail{0};
            void* slots[RING] = {};
        };

        const size_t tail_iters = std::min<size_t>(iters_per_pair, 50'000);

        std::vector<Ring> rings(pair_count);
        // 2*p = producer (alloc) samples, 2*p+1 = consumer (free) samples.
        std::vector<std::vector<double>> block_means(pair_count * 2);
        std::vector<std::vector<double>> per_op(pair_count * 2);

        size_t threads = pair_count * 2;
        SushiRuntime::Test::Barrier go(threads + 1);
        SushiRuntime::Test::Barrier mid(threads + 1);
        SushiRuntime::Test::Barrier done(threads + 1);
        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (size_t p = 0; p < pair_count; ++p)
        {
            // Producer: alloc + first-touch + ring push.
            workers.emplace_back([&, p]
            {
                auto& bm = block_means[2 * p];
                auto& po = per_op[2 * p];
                bm.reserve(iters_per_pair / batch + 1);
                po.reserve(tail_iters);
                Ring& r = rings[p];

                auto push = [&](void* ptr)
                {
                    while (true)
                    {
                        size_t h = r.head.load(std::memory_order_relaxed);
                        size_t t = r.tail.load(std::memory_order_acquire);
                        if (h - t < RING)
                        {
                            r.slots[h & (RING - 1)] = ptr;
                            r.head.store(h + 1, std::memory_order_release);
                            return;
                        }
                        std::this_thread::yield();
                    }
                };

                go.arrive_and_wait();

                // Phase 1: batched alloc timing.
                for (size_t i = 0; i < iters_per_pair; i += batch)
                {
                    size_t n = std::min(batch, iters_per_pair - i);
                    auto t0 = Clock::now();
                    for (size_t j = 0; j < n; ++j)
                    {
                        void* ptr = av.alloc(alloc_size);
                        touch_pages(ptr, alloc_size);
                        push(ptr);
                    }
                    auto t1 = Clock::now();
                    bm.push_back(
                        std::chrono::duration<double, std::nano>(t1 - t0).count()
                            / static_cast<double>(n));
                }

                mid.arrive_and_wait();

                // Phase 2: per-op alloc timing (rdtsc).
                for (size_t k = 0; k < tail_iters; ++k)
                {
                    uint64_t a = HiResTimer::ticks();
                    void* ptr = av.alloc(alloc_size);
                    touch_pages(ptr, alloc_size);
                    push(ptr);
                    uint64_t b = HiResTimer::ticks();
                    po.push_back(op_ns(a, b));
                }

                done.arrive_and_wait();
            });

            // Consumer: ring pop + free (timed).
            workers.emplace_back([&, p]
            {
                auto& bm = block_means[2 * p + 1];
                auto& po = per_op[2 * p + 1];
                bm.reserve(iters_per_pair / batch + 1);
                po.reserve(tail_iters);
                Ring& r = rings[p];
                std::vector<void*> local(batch);

                auto pop = [&]() -> void*
                {
                    while (true)
                    {
                        size_t t = r.tail.load(std::memory_order_relaxed);
                        size_t h = r.head.load(std::memory_order_acquire);
                        if (t < h)
                        {
                            void* ptr = r.slots[t & (RING - 1)];
                            r.tail.store(t + 1, std::memory_order_release);
                            return ptr;
                        }
                        std::this_thread::yield();
                    }
                };

                go.arrive_and_wait();

                // Phase 1: pop a block (handoff excluded), then time the frees.
                for (size_t i = 0; i < iters_per_pair; i += batch)
                {
                    size_t n = std::min(batch, iters_per_pair - i);
                    for (size_t j = 0; j < n; ++j)
                        local[j] = pop();
                    auto t0 = Clock::now();
                    for (size_t j = 0; j < n; ++j)
                        av.free_(local[j]);
                    auto t1 = Clock::now();
                    bm.push_back(
                        std::chrono::duration<double, std::nano>(t1 - t0).count()
                            / static_cast<double>(n));
                }

                mid.arrive_and_wait();

                // Phase 2: per-op free timing (rdtsc); pop() is outside the bracket.
                for (size_t k = 0; k < tail_iters; ++k)
                {
                    void* ptr = pop();
                    uint64_t a = HiResTimer::ticks();
                    av.free_(ptr);
                    uint64_t b = HiResTimer::ticks();
                    po.push_back(op_ns(a, b));
                }

                done.arrive_and_wait();
            });
        }

        auto round_start = Clock::now();
        go.arrive_and_wait();
        mid.arrive_and_wait();
        auto round_end = Clock::now();
        done.arrive_and_wait();

        for (auto& w : workers)
            w.join();

        // Keep the two sides apart so each is reported on its own line.
        std::vector<double> alloc_bm, alloc_po, free_bm, free_po;
        for (size_t p = 0; p < pair_count; ++p)
        {
            auto& a_bm = block_means[2 * p];
            auto& a_po = per_op[2 * p];
            auto& f_bm = block_means[2 * p + 1];
            auto& f_po = per_op[2 * p + 1];
            alloc_bm.insert(alloc_bm.end(), a_bm.begin(), a_bm.end());
            alloc_po.insert(alloc_po.end(), a_po.begin(), a_po.end());
            free_bm.insert(free_bm.end(),   f_bm.begin(), f_bm.end());
            free_po.insert(free_po.end(),   f_po.begin(), f_po.end());
        }

        double total_ns  = std::chrono::duration<double, std::nano>(round_end - round_start).count();
        double total_ops = static_cast<double>(pair_count * iters_per_pair);

        PCResult res;
        res.alloc = compute_latency2(alloc_bm, alloc_po, total_ops, total_ns);
        res.free_ = compute_latency2(free_bm,  free_po,  total_ops, total_ns);
        return res;
    }
}

/**
 * @brief Test fixture for allocator performance benchmarks.
 */
class Performance_AllocatorBench : public ::testing::Test
{
protected:
    SushiRuntime::Execution::RuntimeContext ctx;
    sushi_ptr<USMAllocator> usm;

    void SetUp() override
    {
        usm = ctx.get_allocator();

        // Calibrate the rdtsc tick rate (and read overhead) once.
        HiResTimer::calibrate();

        // Symmetric warmup so neither path pays a cold-start tax first.
        for (int i = 0; i < 2000; ++i)
        {
            void* a = usm->allocate(256);
            void* c = std::malloc(256);
            do_not_optimize(a);
            do_not_optimize(c);
            touch_pages(a, 256);
            touch_pages(c, 256);
            usm->deallocate(a);
            std::free(c);
        }
    }

    /**
     * @brief Contestant table for a given strategy.
     * 
     * @param strat The allocator strategy.
     * @param strat_name The name of the strategy.
     * @return List of contestant vtables.
     */
    std::vector<AllocatorVTable> contestants_for(
        const IAllocatorStrategy& strat, const char* strat_name)
    {
        std::vector<AllocatorVTable> v;

        v.push_back({
            std::string("SushiRuntime [") + strat_name + "]",
            [this, &strat](size_t sz) { return usm->allocate(sz, strat); },
            [this](void* p)           { usm->deallocate(p); }
        });

        return v;
    }

    /**
     * @brief The std::malloc reference floor (non-USM).
     * 
     * @return Allocator vtable for malloc.
     */
    AllocatorVTable malloc_floor()
    {
        return {
            "std::malloc [host floor]",
            [](size_t sz) { return std::malloc(sz); },
            [](void* p)   { std::free(p); }
        };
    }

    /**
     * @brief Run a benchmark across N rounds in randomized order.
     * 
     * First round is discarded as cold-start, reporting the median across the rest.
     * 
     * @tparam BenchFn Benchmark function type.
     * @param title Title of the benchmark.
     * @param contestants List of contestants.
     * @param bench_fn Benchmark function to run.
     * @param warmup_size Warmup allocation size.
     * @param rounds Number of rounds.
     */
    template <typename BenchFn>
    void run_matrix(const std::string& title,
                    std::vector<AllocatorVTable> contestants,
                    BenchFn bench_fn,
                    size_t warmup_size,
                    int rounds = 6)
    {
        std::cout << "\n=== " << title << " ===\n";

        for (auto& c : contestants)
            warmup_scenario(c, warmup_size);

        std::vector<std::vector<LatencyDist>> results(contestants.size());

        std::mt19937 rng(0xC0FFEE);
        std::vector<size_t> order(contestants.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        for (int r = 0; r < rounds; ++r)
        {
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t idx : order)
            {
                auto d = bench_fn(contestants[idx]);
                if (r > 0)
                    results[idx].push_back(d);
            }
        }

        for (size_t i = 0; i < contestants.size(); ++i)
        {
            LatencyDist agg = aggregate_rounds(results[i]);
            print_dist(contestants[i].name, agg);
            sanity_gate(contestants[i].name, agg);
        }
    }

    /**
     * @brief Producer/consumer variant of benchmark matrix execution.
     * 
     * Same scheduling, but each contestant prints two lines ("alloc" and "free")
     * so the handoff sides stay transparent.
     * 
     * @tparam BenchFn Benchmark function type.
     * @param title Title of the benchmark.
     * @param contestants List of contestants.
     * @param bench_fn Benchmark function to run.
     * @param warmup_size Warmup allocation size.
     * @param rounds Number of rounds.
     */
    template <typename BenchFn>
    void run_matrix_pc(const std::string& title,
                       std::vector<AllocatorVTable> contestants,
                       BenchFn bench_fn,
                       size_t warmup_size,
                       int rounds = 6)
    {
        std::cout << "\n=== " << title << " ===\n";

        for (auto& c : contestants)
            warmup_scenario(c, warmup_size);

        std::vector<std::vector<LatencyDist>> alloc_res(contestants.size());
        std::vector<std::vector<LatencyDist>> free_res(contestants.size());

        std::mt19937 rng(0xC0FFEE);
        std::vector<size_t> order(contestants.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        for (int r = 0; r < rounds; ++r)
        {
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t idx : order)
            {
                PCResult pc = bench_fn(contestants[idx]);
                if (r > 0)
                {
                    alloc_res[idx].push_back(pc.alloc);
                    free_res[idx].push_back(pc.free_);
                }
            }
        }

        for (size_t i = 0; i < contestants.size(); ++i)
        {
            LatencyDist a = aggregate_rounds(alloc_res[i]);
            LatencyDist f = aggregate_rounds(free_res[i]);
            print_dist(contestants[i].name + "  alloc", a);
            print_dist(contestants[i].name + "  free",  f);
            sanity_gate(contestants[i].name + " alloc", a);
            sanity_gate(contestants[i].name + " free",  f);
        }
    }
};

/**
 * @brief Tests same-thread alloc/free workload over various sizes.
 * 
 * Represents the magazine best case.
 */
TEST_F(Performance_AllocatorBench, SameThread_Shared_AllSizes)
{
    auto cs = contestants_for(Strategies::Shared::instance(), "Shared");
    cs.push_back(malloc_floor());

    for (size_t sz : {64, 256, 1024, 4096})
    {
        run_matrix(
            "SameThread / Shared / size=" + std::to_string(sz) + "B / 1 thread",
            cs,
            [&](const AllocatorVTable& av)
            {
                return bench_same_thread(av, 1, 200'000, sz);
            },
            sz);
    }
}

/**
 * @brief Tests same-thread alloc/free workload with maximum hardware threads.
 * 
 * Evaluates performance when all cores are concurrently allocating/freeing.
 */
TEST_F(Performance_AllocatorBench, SameThread_Shared_MaxThreads)
{
    auto cs = contestants_for(Strategies::Shared::instance(), "Shared");
    cs.push_back(malloc_floor());

    size_t threads = std::thread::hardware_concurrency();
    for (size_t sz : {64, 256, 1024, 4096})
    {
        run_matrix(
            "SameThread / Shared / size=" + std::to_string(sz) +
                "B / " + std::to_string(threads) + " threads",
            cs,
            [&](const AllocatorVTable& av)
            {
                return bench_same_thread(av, threads, 100'000, sz);
            },
            sz);
    }
}

/**
 * @brief Tests producer/consumer alloc/free workload.
 * 
 * Represents realistic task-graph pattern, which is the magazine worst case.
 */
TEST_F(Performance_AllocatorBench, ProducerConsumer_Shared)
{
    auto cs = contestants_for(Strategies::Shared::instance(), "Shared");
    cs.push_back(malloc_floor());

    size_t pairs = std::max<size_t>(1, std::thread::hardware_concurrency() / 2);
    for (size_t sz : {64, 256, 1024, 4096})
    {
        run_matrix_pc(
            "ProducerConsumer / Shared / size=" + std::to_string(sz) +
                "B / " + std::to_string(pairs) + " pairs",
            cs,
            [&](const AllocatorVTable& av)
            {
                return bench_producer_consumer(av, pairs, 50'000, sz);
            },
            sz);
    }
}
