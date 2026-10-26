/**************************************************************************/
/* test_fluent.cpp                                                        */
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
 * @file test_fluent.cpp
 * @brief Tests the fluent surface: launches, ordering, replay, lifecycle and multidimensional forms.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

using namespace SushiRuntime;

/** @brief Tests the fluent API surface that test_fluent_lifecycle.cpp does not exercise. */
namespace FluentApiTests
{
    /** @brief Tests that run() on a non-empty graph returns a successful, populated RunReport. */
    TEST(Medium_FluentAPI, TryRunSuccessPath)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p   = buf.data();
        auto g = rt.graph();
        g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        auto result = g.run();
        ASSERT_TRUE(result.has_value()) << "run() should succeed on a valid graph.";
        EXPECT_EQ(result->total_tasks_executed, 1u);
        EXPECT_TRUE(result->is_successful);

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
    }

    /** @brief Tests the RunReport fields after a multi-step run. */
    TEST(Medium_FluentAPI, RunReportFieldsAfterMultiStep)
    {
        constexpr std::size_t N     = 32;
        constexpr std::size_t STEPS = 4;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
        // Two independent tasks per step so we can verify the multiplier.
        auto extra = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            extra[i] = 0;
        g.add_rule(extra, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        auto report = g.run(STEPS).value();

        EXPECT_EQ(report.total_tasks_executed, 2 * STEPS);
        EXPECT_TRUE(report.is_successful);
    }

    /** @brief Tests that run_for() with a very short budget runs at least one step. */
    TEST(Medium_FluentAPI, RunForRunsAtLeastOneStep)
    {
        constexpr std::size_t N = 16;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // Zero budget: documented contract says at least one step always runs.
        auto report = g.run_for(std::chrono::milliseconds(0)).value();

        EXPECT_GE(report.total_tasks_executed, 1u)
            << "run_for(0) must still run at least one step.";
        EXPECT_EQ(field[0], 1);
    }

    /** @brief Tests that run_for() with a 50 ms budget completes several steps. */
    TEST(Medium_FluentAPI, RunForRunsMultipleStepsWithinBudget)
    {
        constexpr std::size_t N = 8;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The kernel is warmed first, as run_for fixes its deadline before step one; see README.md.
        g.run().value();

        auto report = g.run_for(std::chrono::milliseconds(50)).value();

        EXPECT_GE(report.total_tasks_executed, 2u)
            << "run_for(50ms) should complete more than one step for a trivial kernel.";
    }

    /** @brief Tests the add_untracked(kernel) overload whose callable takes a sycl::handler. */
    TEST(Medium_FluentAPI, RawHandlerKernelAdd)
    {
        constexpr std::size_t N = 128;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * 2)) << "at i=" << i;
    }

    /** @brief Tests the add_untracked(n, kernel) range kernel overload. */
    TEST(Medium_FluentAPI, RangeKernelAdd)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
    }

    /**
     * @brief Tests that add(reads, writes, n, kernel) orders a writer before the reader that
     *        squares its values.
     */
    TEST(Medium_FluentAPI, PointerTrackedAddOrdering)
    {
        constexpr std::size_t N = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p = buf.data();
        void* key = buf.data();

        auto g = rt.graph();

        // Task 1: write p[i] = i
        g.add(N, Reads(), Writes(BufferRegion(key)),
              [p](std::size_t i) { p[i] = static_cast<int>(i); });

        // Task 2: square in place (RAW on p -> must run after task 1)
        g.add(N, Reads(BufferRegion(key)), Writes(BufferRegion(key)),
              [p](std::size_t i) { p[i] = p[i] * p[i]; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
    }

    /** @brief Tests that the tracked range-kernel overload is ordered after the writer. */
    TEST(Medium_FluentAPI, TrackedRangeKernelOrdering)
    {
        constexpr std::size_t N = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
    }

    /** @brief Tests that the tracked raw-kernel overload is ordered after the writer. */
    TEST(Medium_FluentAPI, TrackedRawKernelOrdering)
    {
        constexpr std::size_t N = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
    }

    /**
     * @brief Tests that the tracked NdRange overload is ordered after the writer and covers the whole
     *        global range.
     */
    TEST(Medium_FluentAPI, TrackedNdRangeOrdering)
    {
        constexpr std::size_t N = 64;   // tiles by 16

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
    }

    /**
     * @brief Tests that two writers of disjoint halves stay unordered and a whole-buffer reader runs
     *        after both.
     */
    TEST(Medium_FluentAPI, SubRegionDisjointWritesThenWholeRead)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto out = rt.buffer<int>(N).value();
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

        g.run().value();

        for (std::size_t i = 0; i < N / 2; ++i)
            ASSERT_EQ(out[i], 1) << "lower half at i=" << i;
        for (std::size_t i = N / 2; i < N; ++i)
            ASSERT_EQ(out[i], 2) << "upper half at i=" << i;
    }

    /**
     * @brief Tests that a standalone Buffer is allocated, seeded, used in a graph task and read back.
     */
    TEST(Medium_FluentAPI, BufferAllocateUseAndReadBack)
    {
        constexpr std::size_t N = 256;

        auto rt  = Runtime::create().value();
        auto src = rt.buffer<float>(N).value();
        auto dst = rt.buffer<float>(N).value();

        for (std::size_t i = 0; i < N; ++i)
        {
            src[i] = static_cast<float>(i);
            dst[i] = 0.0f;
        }

        float* sp = src.data();
        float* dp = dst.data();

        auto g = rt.graph();
        g.add(N, Reads(BufferRegion(sp)), Writes(BufferRegion(dp)),
              [sp, dp](std::size_t i) { dp[i] = sp[i] * 2.0f; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_FLOAT_EQ(dst[i], static_cast<float>(i) * 2.0f) << "at i=" << i;
    }

    // Buffer is move-only: the moved-from handle must become empty.
    TEST(Medium_FluentAPI, BufferMoveLeavesSenderEmpty)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(16).value();
        EXPECT_FALSE(buf.empty());
        EXPECT_EQ(buf.size(), 16u);

        auto moved = std::move(buf);
        EXPECT_TRUE(buf.empty());
        EXPECT_FALSE(moved.empty());
        EXPECT_EQ(moved.size(), 16u);
    }

    /** @brief Tests that Graph::size() tracks the number of add() calls. */
    TEST(Medium_FluentAPI, GraphSizeTracksAdds)
    {
        auto rt = Runtime::create().value();
        auto g  = rt.graph();
        EXPECT_EQ(g.size(), 0u);

        auto f = rt.state<int>(4).value();
        g.add_rule(f, [](std::size_t, const int*, int*) {});
        EXPECT_EQ(g.size(), 1u);

        g.add_rule(f, [](std::size_t, const int*, int*) {});
        EXPECT_EQ(g.size(), 2u);
    }

    /** @brief Tests that the context access and advanced() reach the same RuntimeContext. */
    TEST(Medium_FluentAPI, ContextAccessorIsConsistent)
    {
        auto rt = Runtime::create().value();
        Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(rt);

        // At least a CPU device is always present.
        EXPECT_GE(context.devices().count(), 1u);
        EXPECT_EQ(rt.advanced().device_count(), context.devices().count());
    }

    /** @brief Tests that run(0) returns immediately and reports no work. */
    TEST(Medium_FluentAPI, RunZeroStepsIsNoOp)
    {
        constexpr std::size_t N = 8;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 42;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        auto report = g.run(0).value();

        // No step ran, so field must stay at 42.
        EXPECT_EQ(report.total_tasks_executed, 0u);
        for (std::size_t i = 0; i < N; ++i)
            EXPECT_EQ(field[i], 42) << "run(0) must not execute any steps.";
    }
} // namespace FluentApiTests

/** @brief Tests the multidimensional, variadic, wrapper and captured-argument forms of fluent add(). */
namespace FluentHardeningTests
{
    /**
     * @brief Tests that add_untracked(Extent<2>, kernel) iterates the whole box and writes a row-major
     *        index.
     */
    TEST(Medium_FluentHardening, Extent2DPerElementMap)
    {
        constexpr std::size_t W = 8, H = 6;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(W * H).value();
        for (std::size_t i = 0; i < W * H; ++i)
            buf[i] = -1;

        int* p = buf.data();

        auto g = rt.graph();
        g.add_untracked(Extent{W, H}, [p](sycl::id<2> idx)
        {
            const std::size_t x = idx[0];
            const std::size_t y = idx[1];
            p[y * W + x] = static_cast<int>(y * W + x);
        });
        g.run().value();

        for (std::size_t i = 0; i < W * H; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
    }

    /**
     * @brief Tests that add_untracked(NdRange<2>, kernel) runs a tiled kernel that writes its
     *        global linear index.
     */
    TEST(Medium_FluentHardening, NdRange2DTiledKernel)
    {
        constexpr std::size_t W = 8, H = 8;   // tiles by 4x4

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(W * H).value();
        for (std::size_t i = 0; i < W * H; ++i)
            buf[i] = -1;

        int* p = buf.data();

        auto g = rt.graph();
        g.add_untracked(NdRange{ sycl::range<2>(W, H), sycl::range<2>(4, 4) },
                        [p](sycl::nd_item<2> it)
        {
            const std::size_t x = it.get_global_id(0);
            const std::size_t y = it.get_global_id(1);
            p[y * W + x] = static_cast<int>(y * W + x);
        });
        g.run().value();

        for (std::size_t i = 0; i < W * H; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
    }

    /** @brief Tests that add_rule(Extent<2>, state, kernel) leaves S*y in row y after S steps. */
    TEST(Medium_FluentHardening, State2DSteppedUpdate)
    {
        constexpr std::size_t W = 4, H = 4, S = 3;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(W * H).value();
        for (std::size_t i = 0; i < W * H; ++i)
            field[i] = 0;

        auto g = rt.graph();
        g.add_rule(Extent{W, H}, field, [](sycl::id<2> idx, const int* cur, int* next)
        {
            const std::size_t x = idx[0];
            const std::size_t y = idx[1];
            next[y * W + x] = cur[y * W + x] + static_cast<int>(y);
        });
        g.run(S).value();

        for (std::size_t y = 0; y < H; ++y)
            for (std::size_t x = 0; x < W; ++x)
                ASSERT_EQ(field[y * W + x], static_cast<int>(S * y))
                    << "at (" << x << "," << y << ")";
    }

    /**
     * @brief Tests that add(out, in0, in1, kernel) computes next = cur + a + b from previous-step
     * data.
     */
    TEST(Medium_FluentHardening, VariadicCoupledThreeInputs)
    {
        constexpr std::size_t N = 32;

        auto rt = Runtime::create().value();
        auto u  = rt.state<int>(N).value();
        auto a  = rt.state<int>(N).value();
        auto b  = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
        {
            u[i] = 0;
            a[i] = 2;
            b[i] = 3;
        }

        auto g = rt.graph();
        // a and b have their own (identity) update rules so they advance each step.
        g.add_rule(a, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i]; });
        g.add_rule(b, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i]; });
        g.add_coupled(Coupled(u, a, b), [](std::size_t i, const int* cur, int* next,
                          const int* av, const int* bv)
        {
            next[i] = cur[i] + av[i] + bv[i];
        });
        g.run(4).value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(u[i], 4 * (2 + 3)) << "at i=" << i;
    }

    /** @brief Tests that the Reads and Writes wrappers order a writer before a dependent reader. */
    TEST(Medium_FluentHardening, ReadsWritesWrappersOrdering)
    {
        constexpr std::size_t N = 32;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes{ buf }, [p](std::size_t i) { p[i] = static_cast<int>(i); });
        g.add(N, Reads{ buf }, Writes{ buf }, [p](std::size_t i) { p[i] = p[i] * p[i]; });
        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
    }

    /**
     * @brief Tests that In and Out captured arguments hand the kernel raw pointers and register the
     *        dependency.
     */
    TEST(Medium_FluentHardening, InOutCapturedArguments)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto src = rt.buffer<int>(N).value();
        auto dst = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
        {
            src[i] = static_cast<int>(i);
            dst[i] = 0;
        }

        auto g = rt.graph();
        g.add(Extent{N}, In(src), Out(dst),
              [](sycl::id<1> i, const int* s, int* d) { d[i[0]] = s[i[0]] * 2; });
        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(dst[i], static_cast<int>(i) * 2) << "at i=" << i;
    }

    /** @brief Tests that an interior-only update leaves the one-cell border untouched. */
    TEST(Medium_FluentHardening, InteriorOnlyHaloUpdate)
    {
        constexpr std::size_t W = 6, H = 6;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(W * H).value();
        for (std::size_t i = 0; i < W * H; ++i)
            buf[i] = 0;

        int* p = buf.data();

        auto g = rt.graph();
        // Iterate the (W-2)x(H-2) interior box starting at (1,1); the border is skipped.
        Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
        g.add(interior, Reads{ buf }, Writes{ buf }, [p](sycl::id<2> idx)
        {
            p[idx[1] * W + idx[0]] += 1;
        });
        g.run().value();

        for (std::size_t y = 0; y < H; ++y)
            for (std::size_t x = 0; x < W; ++x)
            {
                const bool border = (x == 0 || y == 0 || x == W - 1 || y == H - 1);
                const int expected = border ? 0 : 1;
                ASSERT_EQ(buf[y * W + x], expected) << "at (" << x << "," << y << ")";
            }
    }
} // namespace FluentHardeningTests

/** @brief Tests State replay, run_until, coupled fields, recompilation and errors in the fluent API. */
namespace FluentLifecycleTests
{
    /**
     * @brief Tests state replay and double-buffering during fluent execution.
     *
     * One update rule increments every element by 1 each step. Verifies that the compiled plan is properly replayed and State is double-buffered so each step reads the previous field and writes the next one.
     */
    TEST(Medium_FluentLifecycle, StateReplayIncrement)
    {
        constexpr std::size_t N     = 1024;
        constexpr std::size_t STEPS = 64;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto sim = rt.graph();
        sim.add_rule(field, [](std::size_t i, const int* cur, int* next)
        {
            next[i] = cur[i] + 1;
        });

        auto report = sim.run(STEPS).value();
        EXPECT_EQ(report.total_tasks_executed, STEPS); // one task replayed STEPS times

        bool ok = true;
        for (std::size_t i = 0; i < N; ++i)
            if (field[i] != static_cast<int>(STEPS)) { ok = false; break; }
        EXPECT_TRUE(ok) << "State replay/double-buffer produced wrong values.";
    }

    /**
     * @brief Tests replay across multiple run() calls.
     *
     * Verifies that the second run resets the node state the first run consumed, ensuring continuous execution.
     */
    TEST(Medium_FluentLifecycle, ReplayAcrossCalls)
    {
        constexpr std::size_t N = 256;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto sim = rt.graph();
        sim.add_rule(field, [](std::size_t i, const int* cur, int* next)
        {
            next[i] = cur[i] + 1;
        });

        sim.run(10).value();
        sim.run(5).value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(field[i], 15);
    }

    /**
     * @brief Tests that run_until stops exactly when the condition is met.
     *
     * Checks its stop condition before each step using the State the previous step produced.
     */
    TEST(Medium_FluentLifecycle, RunUntilStopsOnCondition)
    {
        constexpr std::size_t N = 64;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            field[i] = 0;

        auto sim = rt.graph();
        sim.add_rule(field, [](std::size_t i, const int* cur, int* next)
        {
            next[i] = cur[i] + 1;
        });

        sim.run_until([&]() { return field[0] >= 20; }).value();

        EXPECT_EQ(field[0], 20);
    }

    /**
     * @brief Tests that coupled fields evolve using Jacobi-style updates.
     *
     * Both rules read the previous step's data, verifying the advance-after-step rule rather than per-rule swapping.
     */
    TEST(Medium_FluentLifecycle, CoupledFieldsAreJacobi)
    {
        constexpr std::size_t N     = 128;
        constexpr std::size_t STEPS = 10;

        auto rt = Runtime::create().value();
        auto u  = rt.state<int>(N).value();
        auto v  = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { u[i] = 0; v[i] = 0; }

        auto sim = rt.graph();
        sim.add_rule(v, [](std::size_t i, const int* vc, int* vn)
        {
            vn[i] = vc[i] + 1;
        });
        sim.add_coupled(Coupled(u, v), [](std::size_t i, const int* uc, int* un, const int* vc)
        {
            un[i] = uc[i] + vc[i];
        });

        sim.run(STEPS).value();

        constexpr int expected_v = static_cast<int>(STEPS);             // 10
        constexpr int expected_u = static_cast<int>(STEPS * (STEPS - 1) / 2); // 45
        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(v[i], expected_v);
            ASSERT_EQ(u[i], expected_u);
        }
    }

    /**
     * @brief Tests that mutating the graph recompiles the plan.
     *
     * The next run recompiles with the added rule, and existing state continues from where it left off.
     */
    TEST(Medium_FluentLifecycle, MutationRecompiles)
    {
        constexpr std::size_t N = 128;

        auto rt = Runtime::create().value();
        auto a  = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) a[i] = 0;

        auto sim = rt.graph();
        sim.add_rule(a, [](std::size_t i, const int* c, int* n) { n[i] = c[i] + 1; });
        sim.run(3).value(); // a == 3

        auto b = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) b[i] = 0;
        sim.add_rule(b, [](std::size_t i, const int* c, int* n) { n[i] = c[i] + 2; });
        sim.run(2).value(); // recompiles; a += 2 -> 5, b == 4

        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(a[i], 5);
            ASSERT_EQ(b[i], 4);
        }
    }

    /**
     * @brief Tests that an empty graph is reported as invalid.
     *
     * The non-throwing run() channel reports an InvalidGraph error without throwing an exception.
     */
    TEST(Medium_FluentLifecycle, EmptyGraphIsInvalid)
    {
        auto rt  = Runtime::create().value();
        auto sim = rt.graph();

        auto r = sim.run();
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error().code, ErrorCode::InvalidGraph);
    }

    /**
     * @brief A graph that outlives a Buffer it recorded fails cleanly, not with UB.
     *
     * The graph tracks the liveness of every typed handle it touches, so a run after
     * the Buffer was freed returns a DanglingResource error through run() instead
     * of dispatching a kernel against released memory.
     */
    TEST(Medium_FluentLifecycle, DanglingResourceIsReported)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto sim = rt.graph();

        {
            auto buf = rt.buffer<int>(N).value();
            int* p = buf.data();
            sim.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
        }   // buf is destroyed here, before the graph runs

        auto r = sim.run();
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error().code, ErrorCode::DanglingResource);
    }
} // namespace FluentLifecycleTests
