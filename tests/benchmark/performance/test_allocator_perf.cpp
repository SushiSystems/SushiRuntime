/**************************************************************************/
/* test_allocator_perf.cpp                                                */
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
 * @file test_allocator_perf.cpp
 * @brief Honest performance comparison: SushiRuntime USM pool vs std::malloc.
 *
 * Primary question: how close does the pool/magazine layer (BitmaskMemoryPool
 * + ThreadLocalMagazine) get to the host's highly-tuned std::malloc, while
 * delivering USM (shared CPU/GPU coherence) that malloc cannot? std::malloc is
 * the reference floor -- non-USM, so it does strictly less work; matching it is
 * the bar, beating it on a given pattern is gravy. (A raw sycl::aligned_alloc_*
 * contestant was dropped: an un-pooled allocator is not a fair opponent for a
 * pool, so that comparison was uninformative.)
 *
 * Methodology:
 *   - Same-thread alloc/free (magazine best case)
 *   - Producer/consumer alloc/free (cross-thread, realistic task-graph pattern)
 *   - Multiple size classes (64B, 256B, 1KB, 4KB), Shared (poolable) strategy
 *   - Full-buffer touch (every cache line) to materialize pages, so the real
 *     first-touch cost is paid for every size class, not understated by a
 *     single-byte write.
 *   - Two-source timing per scenario:
 *       * Batched block means feed mean / p50 / throughput. Amortizing the
 *         block over the clock tick keeps the hot-path figure from being
 *         quantized to clock resolution, and keeps per-sample timer overhead
 *         off the headline.
 *       * A separate rdtsc per-op probe feeds p99 / p999 / p9999, so a
 *         magazine-refill spike lands in its own sample instead of being
 *         diluted across a 256-op block. rdtsc (sub-ns) is used here instead
 *         of steady_clock because on virtualized hosts the steady_clock tick
 *         (~100ns) is coarser than a pool op and would floor the tail onto a
 *         quantization artifact rather than the real distribution. The fixed
 *         rdtsc read overhead is calibrated once and subtracted per sample, so
 *         the reported tail is allocator cost, not instrument cost. p9999 is
 *         reported instead of the raw max because the single worst sample is a
 *         one-off OS event (page fault / preemption); p9999 is reproducible.
 *   - Producer/consumer times BOTH sides and reports them SEPARATELY: an
 *     "alloc" line (producer: alloc+touch+handoff) and a "free" line
 *     (consumer: free only). The cross-thread free / remote-free cost is a
 *     first-class number, and merging the two sides never hides which path
 *     carries the cost.
 *   - Randomized contestant order across rounds
 *   - Symmetric warmup, including per-size-class warmup before measurement
 */

#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
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
     * @brief High-resolution per-op timer backed by the CPU timestamp counter.
     *
     * steady_clock on some platforms (notably virtualized WSL / container
     * hosts) has a ~100ns tick -- coarser than a single pool alloc/free. That
     * tick collapses per-op p99/p999 onto a quantization floor and hides the
     * real sub-100ns tail. rdtsc gives sub-nanosecond resolution, so the per-op
     * probe can resolve the pool's true tail instead of the clock's tick.
     *
     * Only same-thread deltas are taken (read before, read after, on the same
     * thread), so cross-core TSC synchronization is irrelevant; only an
     * invariant TSC rate is required, which every modern x86 provides. lfence
     * brackets the read so it is not reordered around the timed work.
     *
     * On non-x86 targets it transparently falls back to steady_clock (and the
     * coarse-tick caveat above applies again there).
     */
    struct HiResTimer
    {
        static inline double ns_per_tick = 1.0;
        // Fixed cost of one ticks()..ticks() bracket with nothing between, in ns.
        // Subtracted from every per-op sample so the reported tail is allocator
        // cost, not the measurement instrument.
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

        /** @brief Pin the tick rate against steady_clock over a busy interval. */
        static void calibrate()
        {
#if defined(SR_HAS_RDTSC)
            const auto window = std::chrono::milliseconds(100);
            uint64_t c0 = ticks();
            auto w0 = Clock::now();
            while (Clock::now() - w0 < window) { do_not_optimize(c0); }
            auto w1 = Clock::now();
            uint64_t c1 = ticks();
            double ns = std::chrono::duration<double, std::nano>(w1 - w0).count();
            ns_per_tick = ns / static_cast<double>(c1 - c0);

            // Minimum back-to-back bracket delta = pure instrument overhead
            // (least perturbed by interrupts). Use the min over many reads.
            uint64_t min_ticks = UINT64_MAX;
            for (int i = 0; i < 4096; ++i) {
                uint64_t a = ticks();
                uint64_t b = ticks();
                if (b - a < min_ticks) min_ticks = b - a;
            }
            overhead_ns = static_cast<double>(min_ticks) * ns_per_tick;
#else
            ns_per_tick = 1.0;
            overhead_ns = 0.0;
#endif
        }
    };

    /**
     * @brief Per-op latency in ns from a tick bracket, with instrument overhead
     *        removed. Clamped at 0 so a sub-overhead op never reads negative.
     */
    inline double op_ns(uint64_t start, uint64_t end)
    {
        double v = static_cast<double>(end - start) * HiResTimer::ns_per_tick
                   - HiResTimer::overhead_ns;
        return v < 0.0 ? 0.0 : v;
    }

    /**
     * @brief Touch every cache line of an allocation to force page commit.
     *
     * The benchmark's honesty hinges on paying real first-touch cost. A pool
     * that recycles already-committed pages writes into warm pages; a raw
     * allocator that returns a fresh mapping faults every line. Touching the
     * whole buffer (stride 64B) rather than just the first byte makes that
     * cost realistic for the larger size classes instead of understating it.
     */
    inline void touch_pages(void* p, size_t size)
    {
        if (!p || size == 0) return;
        auto* c = static_cast<volatile char*>(p);
        for (size_t i = 0; i < size; i += 64) c[i] = 0;
        c[size - 1] = 0;
    }

    /** @brief Per-round latency statistics computed from a sorted sample vector. */
    struct LatencyDist
    {
        double p50 = 0, p99 = 0, p999 = 0, p9999 = 0, mean = 0;
        double ops_per_sec = 0;
    };

    /**
     * @brief Builds a distribution from two sample sources. Sorts both in place.
     *
     * @param block_means Per-op figures from batched timing. Source for mean,
     *                    p50, and (via @p total_ns) throughput -- these are only
     *                    trustworthy when the clock tick is amortized over a
     *                    block, which is exactly what these samples are.
     * @param per_op      True single-op latencies. Source for p99 / p999 / p9999,
     *                    so tail events (e.g. magazine refills) are reported at
     *                    their real per-op magnitude rather than diluted across
     *                    a block. Falls back to @p block_means when empty.
     */
    LatencyDist compute_latency2(std::vector<double>& block_means,
                                 std::vector<double>& per_op,
                                 double total_ops, double total_ns)
    {
        LatencyDist d;
        if (block_means.empty()) return d;

        std::sort(block_means.begin(), block_means.end());
        double sum = 0;
        for (double v : block_means) sum += v;
        d.mean = sum / block_means.size();
        d.p50  = block_means[static_cast<size_t>(0.50 * (block_means.size() - 1))];

        std::vector<double>& tail = per_op.empty() ? block_means : per_op;
        std::sort(tail.begin(), tail.end());
        auto pct = [&](double p) {
            return tail[static_cast<size_t>(p * (tail.size() - 1))];
        };
        d.p99   = pct(0.99);
        d.p999  = pct(0.999);
        d.p9999 = pct(0.9999);   // more reliable than the raw max (single event)

        d.ops_per_sec = (total_ns > 0) ? (total_ops * 1e9 / total_ns) : 0;
        return d;
    }

    /** @brief Per-round results for a producer/consumer contestant, kept split. */
    struct PCResult
    {
        LatencyDist alloc;   // producer side: alloc + first-touch + handoff
        LatencyDist free_;   // consumer side: free() only
    };

    /**
     * @brief Median across measurement rounds, field by field.
     *
     * More robust than the mean against a single-round outlier (e.g. a
     * scheduler preemption spike in one round).
     */
    LatencyDist aggregate_rounds(std::vector<LatencyDist>& rs)
    {
        auto med = [&](double LatencyDist::*field) {
            std::vector<double> v;
            v.reserve(rs.size());
            for (auto& d : rs) v.push_back(d.*field);
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
     * @brief Coarse, timing-agnostic sanity gate for a contestant's result.
     *
     * This is a benchmark, not a perf regression test, so it deliberately does
     * NOT assert a latency threshold (those flake). It only asserts the run
     * made forward progress and did not hang: a deadlock or a path that never
     * returns shows up as zero throughput or an absurd mean, and that should
     * fail ctest. The 10ms ceiling is ~1000x the worst real figure here.
     */
    void sanity_gate(const std::string& name, const LatencyDist& d)
    {
        EXPECT_GT(d.ops_per_sec, 0.0)   << name << ": no forward progress (possible deadlock)";
        EXPECT_LT(d.mean, 1.0e7)        << name << ": mean latency absurd (>10ms), likely a stall";
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
     * @brief A pluggable allocator interface for the benchmark.
     *
     * Lets us swap the contestant (pool, malloc)
     * without changing the harness.
     */
    struct AllocatorVTable
    {
        std::string name;
        std::function<void*(size_t)>  alloc;
        std::function<void(void*)>    free_;
    };

    /**
     * @brief Per-scenario warmup pass.
     *
     * Runs a small alloc/free loop on the contestant at the upcoming
     * benchmark's size class, before any timing begins. This warms the
     * specific magazine bucket that the measurement will use, so that the
     * very first measured iterations do not pay the cold-bucket refill cost.
     *
     * Without this, switching from a 64B benchmark to a 4096B benchmark
     * would cause the first measurements at the new size to hit a cold
     * magazine bucket and refill via bulk_claim -- a tens-of-microseconds
     * spike that pollutes the latency tail of the new scenario.
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
     * @brief Same-thread alloc/free workload. Magazine best case.
     *
     * Each thread runs a tight alloc -> full-buffer touch -> free loop, in two
     * timed phases separated by a barrier:
     *
     *   Phase 1 (batched): every @p batch consecutive ops are timed with a
     *     single clock pair and the block mean is recorded. This amortizes the
     *     steady_clock tick over the block, so mean / p50 / throughput are not
     *     quantized to clock resolution. Only this phase counts toward ops/s.
     *
     *   Phase 2 (per-op probe): a capped run that times each op individually.
     *     These samples feed p99 / p999 / p9999, so a magazine refill spike lands
     *     in its own sample instead of being averaged into a 256-op block. The
     *     pool's real tail is therefore not hidden by batching.
     */
    LatencyDist bench_same_thread(const AllocatorVTable& av,
                                  size_t thread_count,
                                  size_t iters_per_thread,
                                  size_t alloc_size,
                                  size_t batch = 256)
    {
        // Tail-probe budget: enough single-op samples to resolve p999 without
        // doubling total runtime.
        const size_t tail_iters = std::min<size_t>(iters_per_thread, 50'000);

        std::vector<std::vector<double>> block_means(thread_count);
        std::vector<std::vector<double>> per_op(thread_count);
        std::barrier go(thread_count + 1);
        std::barrier mid(thread_count + 1);
        std::barrier done(thread_count + 1);
        std::vector<std::thread> workers;
        workers.reserve(thread_count);

        for (size_t t = 0; t < thread_count; ++t)
        {
            workers.emplace_back([&, t] {
                auto& bm = block_means[t];
                auto& po = per_op[t];
                bm.reserve(iters_per_thread / batch + 1);
                po.reserve(tail_iters);

                go.arrive_and_wait();

                // ── Phase 1: batched timing (mean / p50 / throughput) ──────
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

                // ── Phase 2: per-op timing (p99 / p999 / p9999) ────────────
                // rdtsc, not steady_clock: sub-ns resolution so the tail is not
                // floored at the platform clock tick.
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
        auto round_end = Clock::now();   // batched phase wall time only
        done.arrive_and_wait();

        for (auto& w : workers) w.join();

        std::vector<double> bm_all, po_all;
        for (auto& s : block_means) bm_all.insert(bm_all.end(), s.begin(), s.end());
        for (auto& s : per_op)      po_all.insert(po_all.end(), s.begin(), s.end());

        double total_ns = std::chrono::duration<double, std::nano>(round_end - round_start).count();
        double total_ops = static_cast<double>(thread_count * iters_per_thread);
        return compute_latency2(bm_all, po_all, total_ops, total_ns);
    }

    /**
     * @brief Producer/consumer alloc/free. Magazine worst case (cross-thread free).
     *
     * Half the threads allocate and push pointers into a per-pair SPSC ring;
     * the other half pop and free. This is the realistic task-graph pattern:
     * producer task allocates, consumer task releases.
     *
     * Design notes (transparency):
     *  - The two sides are timed and reported SEPARATELY, not merged: the
     *    producer line is alloc + full-buffer touch + ring handoff, the
     *    consumer line is free() only. Merging the two would hide which side
     *    carries the cost and make p50/p99 bimodal; split lines say plainly
     *    what each path costs. The cross-thread free / remote-free cost is now
     *    a first-class number, not folded into the alloc figure.
     *  - Two phases per side, as in the same-thread bench: a batched phase
     *    (mean / p50 / throughput, overhead-free) and an rdtsc per-op probe
     *    (p99 / p999 / p9999, sub-ns resolution). The consumer's batched phase
     *    pops a whole block first, then times only the free() calls, so handoff
     *    backpressure is excluded from the measured free cost. With RING=1024
     *    well above batch, the producer rarely blocks on this, so the pipeline
     *    rate is preserved.
     *  - Throughput is the shared pipeline rate (pairs processed / phase-1 wall
     *    time); both lines report the same value by construction.
     *  - RING is bounded so the producer cannot race ahead and pile up
     *    unfreed allocations, which would distort magazine state. This is
     *    a deliberate choice; an unbounded ring would make alloc latency
     *    look better but at the cost of unrealistic memory pressure.
     */
    PCResult bench_producer_consumer(const AllocatorVTable& av,
                                     size_t pair_count,
                                     size_t iters_per_pair,
                                     size_t alloc_size,
                                     size_t batch = 256)
    {
        constexpr size_t RING = 1024;
        struct Ring {
            alignas(64) std::atomic<size_t> head{0};
            alignas(64) std::atomic<size_t> tail{0};
            void* slots[RING] = {};
        };

        const size_t tail_iters = std::min<size_t>(iters_per_pair, 50'000);

        std::vector<Ring> rings(pair_count);
        // Index 2*p = producer (alloc) samples, 2*p+1 = consumer (free) samples.
        std::vector<std::vector<double>> block_means(pair_count * 2);
        std::vector<std::vector<double>> per_op(pair_count * 2);

        size_t threads = pair_count * 2;
        std::barrier go(threads + 1);
        std::barrier mid(threads + 1);
        std::barrier done(threads + 1);
        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (size_t p = 0; p < pair_count; ++p)
        {
            // ── Producer: alloc + first-touch + ring push ──────────────────
            workers.emplace_back([&, p] {
                auto& bm = block_means[2 * p];
                auto& po = per_op[2 * p];
                bm.reserve(iters_per_pair / batch + 1);
                po.reserve(tail_iters);
                Ring& r = rings[p];

                auto push = [&](void* ptr) {
                    while (true) {
                        size_t h = r.head.load(std::memory_order_relaxed);
                        size_t t = r.tail.load(std::memory_order_acquire);
                        if (h - t < RING) {
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

            // ── Consumer: ring pop + free (now timed) ──────────────────────
            workers.emplace_back([&, p] {
                auto& bm = block_means[2 * p + 1];
                auto& po = per_op[2 * p + 1];
                bm.reserve(iters_per_pair / batch + 1);
                po.reserve(tail_iters);
                Ring& r = rings[p];
                std::vector<void*> local(batch);

                auto pop = [&]() -> void* {
                    while (true) {
                        size_t t = r.tail.load(std::memory_order_relaxed);
                        size_t h = r.head.load(std::memory_order_acquire);
                        if (t < h) {
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
                    for (size_t j = 0; j < n; ++j) local[j] = pop();
                    auto t0 = Clock::now();
                    for (size_t j = 0; j < n; ++j) av.free_(local[j]);
                    auto t1 = Clock::now();
                    bm.push_back(
                        std::chrono::duration<double, std::nano>(t1 - t0).count()
                            / static_cast<double>(n));
                }

                mid.arrive_and_wait();

                // Phase 2: per-op free timing (rdtsc). pop() is outside the
                // timed bracket, so only the free() cost is captured.
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

        for (auto& w : workers) w.join();

        // Index 2*p = producer (alloc), 2*p+1 = consumer (free). Keep the two
        // sides apart so each is reported on its own line.
        std::vector<double> alloc_bm, alloc_po, free_bm, free_po;
        for (size_t p = 0; p < pair_count; ++p)
        {
            auto& a_bm = block_means[2 * p];     auto& a_po = per_op[2 * p];
            auto& f_bm = block_means[2 * p + 1]; auto& f_po = per_op[2 * p + 1];
            alloc_bm.insert(alloc_bm.end(), a_bm.begin(), a_bm.end());
            alloc_po.insert(alloc_po.end(), a_po.begin(), a_po.end());
            free_bm.insert(free_bm.end(),   f_bm.begin(), f_bm.end());
            free_po.insert(free_po.end(),   f_po.begin(), f_po.end());
        }

        double total_ns = std::chrono::duration<double, std::nano>(round_end - round_start).count();
        double total_ops = static_cast<double>(pair_count * iters_per_pair);

        PCResult res;
        res.alloc = compute_latency2(alloc_bm, alloc_po, total_ops, total_ns);
        res.free_ = compute_latency2(free_bm,  free_po,  total_ops, total_ns);
        return res;
    }
}

class AllocatorBench : public ::testing::Test
{
protected:
    SushiRuntime::Execution::RuntimeContext ctx;
    sushi_ptr<USMAllocator> usm;

    void SetUp() override
    {
        usm = ctx.get_allocator();

        // Calibrate the rdtsc tick rate (and its read overhead) once; the
        // per-op probes depend on both.
        HiResTimer::calibrate();

        // Symmetric global warmup: hit the pool and malloc paths so neither
        // pays a cold-start tax on the first measured scenario. Per-scenario
        // warmup (warmup_scenario) handles size-class transitions.
        for (int i = 0; i < 2000; ++i) {
            void* a = usm->allocate(256);
            void* c = std::malloc(256);
            do_not_optimize(a); do_not_optimize(c);
            touch_pages(a, 256); touch_pages(c, 256);
            usm->deallocate(a);
            std::free(c);
        }
    }

    /** @brief Builds the contestant table for a given strategy. */
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

    /** @brief Adds the malloc reference floor (non-USM). */
    AllocatorVTable malloc_floor()
    {
        return {
            "std::malloc [host floor]",
            [](size_t sz) { return std::malloc(sz); },
            [](void* p)   { std::free(p); }
        };
    }

    /**
     * @brief Runs a benchmark function across N rounds in randomized order,
     *        with a per-scenario warmup pass before the first measured round.
     *
     * The first round's results are discarded as cold-start noise. Reported
     * statistics are the median across the remaining rounds, which is more
     * robust than the mean against single-round outliers (e.g., scheduler
     * preemption spikes).
     *
     * @param title Human-readable scenario label.
     * @param contestants The allocators to compare.
     * @param bench_fn The benchmark function to run.
     * @param warmup_size Allocation size to use for per-scenario warmup.
     * @param rounds Number of measurement rounds (first is discarded).
     */
    template <typename BenchFn>
    void run_matrix(const std::string& title,
                    std::vector<AllocatorVTable> contestants,
                    BenchFn bench_fn,
                    size_t warmup_size,
                    int rounds = 6)
    {
        std::cout << "\n=== " << title << " ===\n";

        // Per-scenario warmup: each contestant gets a chance to warm its
        // size-class-specific magazine bucket (or, for non-pool allocators,
        // its internal caches) before measurement begins. Crucial when
        // moving across size classes within a single test.
        for (auto& c : contestants) {
            warmup_scenario(c, warmup_size);
        }

        std::vector<std::vector<LatencyDist>> results(contestants.size());

        std::mt19937 rng(0xC0FFEE);
        std::vector<size_t> order(contestants.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;

        for (int r = 0; r < rounds; ++r) {
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t idx : order) {
                auto d = bench_fn(contestants[idx]);
                if (r > 0) results[idx].push_back(d);
            }
        }

        for (size_t i = 0; i < contestants.size(); ++i) {
            LatencyDist agg = aggregate_rounds(results[i]);
            print_dist(contestants[i].name, agg);
            sanity_gate(contestants[i].name, agg);
        }
    }

    /**
     * @brief Producer/consumer variant of run_matrix.
     *
     * Identical scheduling (warmup, randomized order, first round discarded,
     * median across rounds), but each contestant yields a @ref PCResult and is
     * printed as two lines -- "<name> alloc" and "<name> free" -- so the two
     * sides of the handoff are reported transparently instead of merged.
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
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;

        for (int r = 0; r < rounds; ++r) {
            std::shuffle(order.begin(), order.end(), rng);
            for (size_t idx : order) {
                PCResult pc = bench_fn(contestants[idx]);
                if (r > 0) {
                    alloc_res[idx].push_back(pc.alloc);
                    free_res[idx].push_back(pc.free_);
                }
            }
        }

        for (size_t i = 0; i < contestants.size(); ++i) {
            LatencyDist a = aggregate_rounds(alloc_res[i]);
            LatencyDist f = aggregate_rounds(free_res[i]);
            print_dist(contestants[i].name + "  alloc", a);
            print_dist(contestants[i].name + "  free",  f);
            sanity_gate(contestants[i].name + " alloc", a);
            sanity_gate(contestants[i].name + " free",  f);
        }
    }
};

// ============================================================================
// Same-thread workload: magazine best case
// ============================================================================

TEST_F(AllocatorBench, SameThread_Shared_AllSizes)
{
    auto cs = contestants_for(Strategies::Shared::instance(), "Shared");
    cs.push_back(malloc_floor());

    for (size_t sz : {64, 256, 1024, 4096}) {
        run_matrix(
            "SameThread / Shared / size=" + std::to_string(sz) + "B / 1 thread",
            cs,
            [&](const AllocatorVTable& av) {
                return bench_same_thread(av, 1, 200'000, sz);
            },
            sz);
    }
}

TEST_F(AllocatorBench, SameThread_Shared_MaxThreads)
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
            [&](const AllocatorVTable& av) {
                return bench_same_thread(av, threads, 100'000, sz);
            },
            sz);
    }
}

// ============================================================================
// Producer/consumer: realistic task-graph pattern, magazine worst case
// ============================================================================

TEST_F(AllocatorBench, ProducerConsumer_Shared)
{
    auto cs = contestants_for(Strategies::Shared::instance(), "Shared");
    cs.push_back(malloc_floor());

    size_t pairs = std::max<size_t>(1, std::thread::hardware_concurrency() / 2);
    for (size_t sz : {64, 256, 1024, 4096}) {
        run_matrix_pc(
            "ProducerConsumer / Shared / size=" + std::to_string(sz) +
                "B / " + std::to_string(pairs) + " pairs",
            cs,
            [&](const AllocatorVTable& av) {
                return bench_producer_consumer(av, pairs, 50'000, sz);
            },
            sz);
    }
}