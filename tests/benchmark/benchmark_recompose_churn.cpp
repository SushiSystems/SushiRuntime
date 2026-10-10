/**************************************************************************/
/* benchmark_recompose_churn.cpp                                          */
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
 * @file benchmark_recompose_churn.cpp
 * @brief Measures the wall time and allocations of one dirty-region commit against a pure replay.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // The counter is off outside a measured window; see tests/benchmark/README.md.
    std::atomic<bool>   g_count_on{false};
    std::atomic<size_t> g_alloc_n{0};
    std::atomic<size_t> g_alloc_bytes{0};

    inline void count_alloc(std::size_t n) noexcept
    {
        if (g_count_on.load(std::memory_order_relaxed))
        {
            g_alloc_n.fetch_add(1, std::memory_order_relaxed);
            g_alloc_bytes.fetch_add(n, std::memory_order_relaxed);
        }
    }

    struct AllocScope
    {
        size_t n0, b0;
        AllocScope()
        {
            n0 = g_alloc_n.load(std::memory_order_relaxed);
            b0 = g_alloc_bytes.load(std::memory_order_relaxed);
            g_count_on.store(true, std::memory_order_relaxed);
        }
        void stop() { g_count_on.store(false, std::memory_order_relaxed); }
        size_t allocs() const { return g_alloc_n.load(std::memory_order_relaxed) - n0; }
        size_t bytes()  const { return g_alloc_bytes.load(std::memory_order_relaxed) - b0; }
    };
}

// One replacement serves the whole benchmark binary and counts only while g_count_on is set.
void* operator new(std::size_t n)            { count_alloc(n); void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n)          { count_alloc(n); void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void  operator delete(void* p) noexcept              { std::free(p); }
void  operator delete[](void* p) noexcept            { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

TEST(Performance_RecomposeChurn, AllocationsAndTimePerCommit)
{
    constexpr std::size_t N       = 8;    // small region: substrate is many-small, not few-large
    constexpr std::size_t REGIONS = 256;  // steady-state world size
    constexpr std::size_t COMMITS = 64;   // dirty commits measured

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);     // deterministic; keep the migrator off the numbers

    std::vector<Buffer<int>> bufs;
    bufs.reserve(REGIONS);
    for (std::size_t r = 0; r < REGIONS; ++r)
        bufs.push_back(rt.buffer<int>(N));
    for (auto& buf : bufs)
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

    auto world = rt.dynamic_graph();
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        int* p = bufs[r].data();
        world.region(r).add(API::Reads{}, API::Writes(bufs[r]), N,
                            [p](std::size_t i) { p[i] += 1; });
    }

    // Warm up: first run compiles the whole world once (not a "commit").
    world.run(1);
    ASSERT_EQ(world.compile_count(), 1u);

    using clock = std::chrono::steady_clock;

    // The baseline is a replay step with no dirty region, the best of three.
    double best_replay_ms = 1e300;
    size_t replay_allocs  = 0;
    for (int r = 0; r < 3; ++r)
    {
        AllocScope as;
        const auto t0 = clock::now();
        world.run(1);
        const auto t1 = clock::now();
        as.stop();
        best_replay_ms = std::min(best_replay_ms,
            std::chrono::duration<double, std::milli>(t1 - t0).count());
        replay_allocs = as.allocs();
    }
    // compile_count must NOT have moved: pure replay never recomposes.
    const std::size_t compiles_after_warm_replay = world.compile_count();

    // Each commit marks one region dirty and steps, so it recomposes exactly once.
    double total_commit_ms = 0.0;
    double best_commit_ms  = 1e300;
    double worst_commit_ms = 0.0;
    size_t total_commit_allocs = 0;
    size_t best_commit_allocs  = static_cast<size_t>(-1);

    std::size_t expected_compiles = compiles_after_warm_replay;
    for (std::size_t m = 0; m < COMMITS; ++m)
    {
        const std::size_t r = (m * 7 + 3) % REGIONS;
        int* p = bufs[r].data();

        AllocScope as;
        const auto t0 = clock::now();
        world.region(r).add(API::Reads{}, API::Writes(bufs[r]), N,
                            [p](std::size_t i) { p[i] += 1; });
        world.run(1);
        const auto t1 = clock::now();
        as.stop();

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_commit_ms += ms;
        best_commit_ms   = std::min(best_commit_ms, ms);
        worst_commit_ms  = std::max(worst_commit_ms, ms);
        total_commit_allocs += as.allocs();
        best_commit_allocs   = std::min(best_commit_allocs, as.allocs());

        ++expected_compiles;
        ASSERT_EQ(world.compile_count(), expected_compiles)
            << "exactly one recompose per mutation at m=" << m;
    }

    const double avg_commit_ms = total_commit_ms / double(COMMITS);
    const double avg_commit_allocs = double(total_commit_allocs) / double(COMMITS);
    const double recompose_ms = avg_commit_ms - best_replay_ms;
    const double recompose_allocs =
        double(best_commit_allocs) - double(replay_allocs);

    std::printf("\n=== Recompose churn (%zu regions, N=%zu, %zu commits) ===\n",
                REGIONS, N, COMMITS);
    std::printf("  Pure replay step   : %8.4f ms   | %8zu allocs/step\n",
                best_replay_ms, replay_allocs);
    std::printf("  Commit (recompose+step): avg %8.4f ms  best %8.4f  worst %8.4f\n",
                avg_commit_ms, best_commit_ms, worst_commit_ms);
    std::printf("  Commit allocations : avg %10.1f  best %8zu   (Linux/CI: incl. lib; Win: exe-side only)\n",
                avg_commit_allocs, best_commit_allocs);
    std::printf("  Recompose-only delta: %8.4f ms   | %8.0f allocs   (commit - pure-replay)\n",
                recompose_ms, recompose_allocs);
    std::printf("  -> gates B2: if recompose allocs/ms dominate, node/AccessNode/event\n"
                "     churn is the target; if the pure-replay step already allocates\n"
                "     heavily, redirect effort to steady-state execution instead.\n");

    // A nonzero, uniform value per region is the smoke check; see tests/benchmark/README.md.
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        const int v0 = bufs[r][0];
        EXPECT_GT(v0, 0) << "region " << r << " never ran";
        for (std::size_t i = 1; i < N; ++i)
            EXPECT_EQ(bufs[r][i], v0) << "region " << r << " not uniform at i=" << i;
    }
    EXPECT_GT(world.compile_count(), 1u);
}
