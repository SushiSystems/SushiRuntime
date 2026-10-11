/**************************************************************************/
/* test_fluent_api.cpp                                                    */
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
 * @file test_fluent_api.cpp
 * @brief Tests the fluent API surface that test_fluent_lifecycle.cpp does not exercise.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/** @brief Tests that try_run() on a non-empty graph returns a successful, populated RunReport. */
TEST(Integration_FluentAPI, TryRunSuccessPath)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p   = buf.data();
    auto g = rt.graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto result = g.try_run();
    ASSERT_TRUE(result.has_value()) << "try_run() should succeed on a valid graph.";
    EXPECT_EQ(result->total_tasks_executed, 1u);
    EXPECT_TRUE(result->is_successful);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}

/** @brief Tests the RunReport fields after a multi-step run. */
TEST(Integration_FluentAPI, RunReportFieldsAfterMultiStep)
{
    constexpr std::size_t N     = 32;
    constexpr std::size_t STEPS = 4;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
    // Two independent tasks per step so we can verify the multiplier.
    auto extra = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        extra[i] = 0;
    g.add(extra, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    auto report = g.run(STEPS);

    EXPECT_EQ(report.total_tasks_executed, 2 * STEPS);
    EXPECT_TRUE(report.is_successful);
    EXPECT_GE(report.total_duration_ms, 0.0);
}

/** @brief Tests that run_for() with a very short budget runs at least one step. */
TEST(Integration_FluentAPI, RunForRunsAtLeastOneStep)
{
    constexpr std::size_t N = 16;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Zero budget: documented contract says at least one step always runs.
    auto report = g.run_for(std::chrono::milliseconds(0));

    EXPECT_GE(report.total_tasks_executed, 1u)
        << "run_for(0) must still run at least one step.";
    EXPECT_EQ(field[0], 1);
}

/** @brief Tests that run_for() with a 50 ms budget completes several steps. */
TEST(Integration_FluentAPI, RunForRunsMultipleStepsWithinBudget)
{
    constexpr std::size_t N = 8;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // The kernel is warmed first, as run_for fixes its deadline before step one; see README.md.
    g.run();

    auto report = g.run_for(std::chrono::milliseconds(50));

    EXPECT_GE(report.total_tasks_executed, 2u)
        << "run_for(50ms) should complete more than one step for a trivial kernel.";
    EXPECT_GE(report.total_duration_ms, 0.0);
}

/** @brief Tests the add_untracked(fn) overload whose callable takes a sycl::handler. */
TEST(Integration_FluentAPI, RawHandlerKernelAdd)
{
    constexpr std::size_t N = 128;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add_untracked([p](sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(N), [p](sycl::id<1> i)
        {
            p[i[0]] = static_cast<int>(i[0]) * 2;
        });
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * 2)) << "at i=" << i;
}

/** @brief Tests the add_untracked(n, fn) range kernel overload. */
TEST(Integration_FluentAPI, RangeKernelAdd)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = -1;

    int* p = buf.data();

    auto g = rt.graph();
    g.add_untracked(N, [p](std::size_t begin, std::size_t end, sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(end - begin), [p, begin](sycl::id<1> idx)
        {
            p[begin + idx[0]] = static_cast<int>(begin + idx[0]);
        });
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}

/**
 * @brief Tests that add(reads, writes, n, fn) orders a writer before the reader that squares its
 *        values.
 */
TEST(Integration_FluentAPI, PointerTrackedAddOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();
    void* key = buf.data();

    auto g = rt.graph();

    // Task 1: write p[i] = i
    g.add(N, Reads(), Writes(key), [p](std::size_t i) { p[i] = static_cast<int>(i); });

    // Task 2: square in place (RAW on p -> must run after task 1)
    g.add(N, Reads(key), Writes(key), [p](std::size_t i) { p[i] = p[i] * p[i]; });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

/** @brief Tests that the tracked range-kernel overload is ordered after the writer. */
TEST(Integration_FluentAPI, TrackedRangeKernelOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(N, Reads(buf), Writes(buf),
          [p](std::size_t begin, std::size_t end, sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(end - begin), [p, begin](sycl::id<1> idx)
        {
            const std::size_t i = begin + idx[0];
            p[i] = p[i] * p[i];
        });
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

/** @brief Tests that the tracked raw-kernel overload is ordered after the writer. */
TEST(Integration_FluentAPI, TrackedRawKernelOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(Reads(buf), Writes(buf), [p](sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(N), [p](sycl::id<1> idx)
        {
            p[idx[0]] = p[idx[0]] * p[idx[0]];
        });
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

/**
 * @brief Tests that the tracked NdRange overload is ordered after the writer and covers the whole
 *        global range.
 */
TEST(Integration_FluentAPI, TrackedNdRangeOrdering)
{
    constexpr std::size_t N = 64;   // tiles by 16

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(NdRange{ sycl::range<1>(N), sycl::range<1>(16) }, Reads(buf),
          Writes(buf),
          [p](sycl::nd_item<1> it)
    {
        const std::size_t i = it.get_global_id(0);
        p[i] = p[i] * p[i];
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

/**
 * @brief Tests that two writers of disjoint halves stay unordered and a whole-buffer reader runs
 *        after both.
 */
TEST(Integration_FluentAPI, SubRegionDisjointWritesThenWholeRead)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto out = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
    {
        buf[i] = 0;
        out[i] = -1;
    }

    int* p = buf.data();
    int* o = out.data();

    auto g = rt.graph();

    // Lower half := 1, upper half := 2. Disjoint regions of buf -> unordered.
    g.add(N / 2, Reads(), Writes(buf.region({0, N / 2})),
          [p](std::size_t i) { p[i] = 1; });
    g.add(N / 2, Reads(), Writes(buf.region({N / 2, N / 2})),
          [p](std::size_t i) { p[N / 2 + i] = 2; });

    // Whole-buffer read overlaps both halves -> ordered after both writers.
    g.add(N, Reads(buf), Writes(out),
          [p, o](std::size_t i) { o[i] = p[i]; });

    g.run();

    for (std::size_t i = 0; i < N / 2; ++i)
        ASSERT_EQ(out[i], 1) << "lower half at i=" << i;
    for (std::size_t i = N / 2; i < N; ++i)
        ASSERT_EQ(out[i], 2) << "upper half at i=" << i;
}

/**
 * @brief Tests that a standalone Buffer is allocated, seeded, used in a graph task and read back.
 */
TEST(Integration_FluentAPI, BufferAllocateUseAndReadBack)
{
    constexpr std::size_t N = 256;

    auto rt  = Runtime::create();
    auto src = rt.buffer<float>(N);
    auto dst = rt.buffer<float>(N);

    for (std::size_t i = 0; i < N; ++i)
    {
        src[i] = static_cast<float>(i);
        dst[i] = 0.0f;
    }

    float* sp = src.data();
    float* dp = dst.data();

    auto g = rt.graph();
    g.add(N, Reads(sp), Writes(dp), [sp, dp](std::size_t i) { dp[i] = sp[i] * 2.0f; });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_FLOAT_EQ(dst[i], static_cast<float>(i) * 2.0f) << "at i=" << i;
}

// Buffer is move-only: the moved-from handle must become empty.
TEST(Integration_FluentAPI, BufferMoveLeavesSenderEmpty)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(16);
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 16u);

    auto moved = std::move(buf);
    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(moved.empty());
    EXPECT_EQ(moved.size(), 16u);
}

/** @brief Tests that Graph::size() tracks the number of add() calls. */
TEST(Integration_FluentAPI, GraphSizeTracksAdds)
{
    auto rt = Runtime::create();
    auto g  = rt.graph();
    EXPECT_EQ(g.size(), 0u);

    auto f = rt.state<int>(4);
    g.add(f, [](std::size_t, const int*, int*) {});
    EXPECT_EQ(g.size(), 1u);

    g.add(f, [](std::size_t, const int*, int*) {});
    EXPECT_EQ(g.size(), 2u);
}

/** @brief Tests that unsafe_context() and advanced() reach the same RuntimeContext. */
TEST(Integration_FluentAPI, ContextAccessorIsConsistent)
{
    auto rt = Runtime::create();
    Internal::Execution::RuntimeContext& ctx = rt.unsafe_context();

    // At least a CPU device is always present.
    EXPECT_GE(ctx.devices().count(), 1u);
    EXPECT_EQ(rt.advanced().device_count(), ctx.devices().count());
}

/** @brief Tests that run(0) returns immediately and reports no work. */
TEST(Integration_FluentAPI, RunZeroStepsIsNoOp)
{
    constexpr std::size_t N = 8;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 42;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    auto report = g.run(0);

    // No step ran, so field must stay at 42.
    EXPECT_EQ(report.total_tasks_executed, 0u);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(field[i], 42) << "run(0) must not execute any steps.";
}
