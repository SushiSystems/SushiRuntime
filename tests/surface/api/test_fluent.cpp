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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_fluent.cpp
 * @brief Tests the fluent surface: launches, ordering, replay, lifecycle and multidimensional forms.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns 0, 1, ... multiplied by @p factor, @p count elements. */
    std::vector<int> ramp_times(std::size_t count, int factor)
    {
        std::vector<int> values = TestSupport::ramp<int>(count);
        for (int& value : values)
            value *= factor;
        return values;
    }

    /** @brief Returns the squares of 0, 1, ..., @p count elements. */
    std::vector<int> squares(std::size_t count)
    {
        std::vector<int> values = TestSupport::ramp<int>(count);
        for (int& value : values)
            value *= value;
        return values;
    }

    /** @brief Returns @p rows rows of @p width elements, each element holding the row index times @p factor. */
    std::vector<int> row_index_times(std::size_t width, std::size_t rows, int factor)
    {
        std::vector<int> values(width * rows);
        for (std::size_t y = 0; y < rows; ++y)
            std::fill(values.begin() + y * width, values.begin() + (y + 1) * width,
                      static_cast<int>(y) * factor);
        return values;
    }

    /** @brief Returns a width by height grid that holds @p inside in the cells off its one-cell border and @p border on it. */
    std::vector<int> bordered_grid(std::size_t width, std::size_t height, int inside, int border)
    {
        std::vector<int> grid(width * height, border);
        for (std::size_t y = 1; y + 1 < height; ++y)
            std::fill(grid.begin() + y * width + 1, grid.begin() + y * width + width - 1, inside);
        return grid;
    }

    /** @brief Records into @p graph @p count rules over @p field that change nothing. */
    void record_noop_rules(Graph& graph, State<int>& field, std::size_t count)
    {
        for (std::size_t added = 0; added < count; ++added)
            graph.add_rule(field, [](std::size_t, const int*, int*) {});
    }

    /** @brief Records into @p graph one rule that adds @p increment to every cell of @p field on each step. */
    void record_increment(Graph& graph, State<int>& field, int increment)
    {
        graph.add_rule(field, [increment](std::size_t i, const int* cur, int* next)
        {
            next[i] = cur[i] + increment;
        });
    }
} // namespace

/** @brief Fixture of the fluent surface tests, which run on the program's runtime. */
class Medium_FluentAPI : public TestSupport::RuntimeTest
{
};

/** @brief Tests the RunReport fields after a multi-step run. */
TEST_F(Medium_FluentAPI, RunReportFieldsAfterMultiStep)
{
    constexpr std::size_t N     = 32;
    constexpr std::size_t STEPS = 4;
    auto field = runtime().state<int>(N).value();
    auto extra = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 0);
    TestSupport::fill_with(extra, 0);
    auto g = runtime().graph();
    record_increment(g, field, 1);
    record_increment(g, extra, 1);

    auto report = g.run(STEPS).value();

    EXPECT_EQ(report.total_tasks_executed, 2 * STEPS);
    EXPECT_TRUE(report.is_successful);
}

/** @brief Tests that run_for() with a zero budget still runs one step. */
TEST_F(Medium_FluentAPI, RunForRunsAtLeastOneStep)
{
    constexpr std::size_t N = 16;
    auto field = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 0);
    auto g = runtime().graph();
    record_increment(g, field, 1);

    auto report = g.run_for(std::chrono::milliseconds(0)).value();

    EXPECT_GE(report.total_tasks_executed, 1u) << "run_for(0) must still run at least one step.";
    EXPECT_EQ(field[0], 1);
}

/** @brief Tests the add_untracked(kernel) overload whose callable takes a sycl::handler. */
TEST_F(Medium_FluentAPI, RawHandlerKernelAdd)
{
    constexpr std::size_t N = 128;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add_untracked([p](sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(N), [p](sycl::id<1> i)
        {
            p[i[0]] = static_cast<int>(i[0]) * 2;
        });
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), ramp_times(N, 2));
}

/** @brief Tests the add_untracked(n, kernel) range kernel overload. */
TEST_F(Medium_FluentAPI, RangeKernelAdd)
{
    constexpr std::size_t N = 64;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, -1);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add_untracked(N, [p](std::size_t begin, std::size_t end, sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(end - begin), [p, begin](sycl::id<1> idx)
        {
            p[begin + idx[0]] = static_cast<int>(begin + idx[0]);
        });
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(N));
}

/**
 * @brief Tests that add(reads, writes, n, kernel) orders a writer before the reader that
 *        squares its values.
 */
TEST_F(Medium_FluentAPI, PointerTrackedAddOrdering)
{
    constexpr std::size_t N = 32;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    void* key = buf.data();
    auto g = runtime().graph();
    g.add(N, Reads(), Writes(BufferRegion(key)), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(N, Reads(BufferRegion(key)), Writes(BufferRegion(key)),
          [p](std::size_t i) { p[i] = p[i] * p[i]; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), squares(N));
}

/** @brief Tests that the tracked range-kernel overload is ordered after the writer. */
TEST_F(Medium_FluentAPI, TrackedRangeKernelOrdering)
{
    constexpr std::size_t N = 32;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
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

    EXPECT_EQ(TestSupport::contents(buf), squares(N));
}

/** @brief Tests that the tracked raw-kernel overload is ordered after the writer. */
TEST_F(Medium_FluentAPI, TrackedRawKernelOrdering)
{
    constexpr std::size_t N = 32;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(Reads(buf), Writes(buf), [p](sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(N), [p](sycl::id<1> idx)
        {
            p[idx[0]] = p[idx[0]] * p[idx[0]];
        });
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), squares(N));
}

/**
 * @brief Tests that the tracked NdRange overload is ordered after the writer and covers the whole
 *        global range.
 */
TEST_F(Medium_FluentAPI, TrackedNdRangeOrdering)
{
    constexpr std::size_t N = 64;   // tiles by 16
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(NdRange{ sycl::range<1>(N), sycl::range<1>(16) }, Reads(buf), Writes(buf),
          [p](sycl::nd_item<1> it)
    {
        const std::size_t i = it.get_global_id(0);
        p[i] = p[i] * p[i];
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), squares(N));
}

/**
 * @brief Tests that two writers of disjoint halves stay unordered and a whole-buffer reader runs
 *        after both.
 */
TEST_F(Medium_FluentAPI, SubRegionDisjointWritesThenWholeRead)
{
    constexpr std::size_t N = 64;
    auto buf = runtime().buffer<int>(N).value();
    auto out = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    TestSupport::fill_with(out, -1);
    int* p = buf.data();
    int* o = out.data();
    auto g = runtime().graph();
    // Lower half := 1, upper half := 2. Disjoint regions of buf -> unordered.
    g.add(N / 2, Reads(), Writes(buf.region({0, N / 2})), [p](std::size_t i) { p[i] = 1; });
    g.add(N / 2, Reads(), Writes(buf.region({N / 2, N / 2})), [p](std::size_t i) { p[N / 2 + i] = 2; });
    // Whole-buffer read overlaps both halves -> ordered after both writers.
    g.add(N, Reads(buf), Writes(out), [p, o](std::size_t i) { o[i] = p[i]; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(out), TestSupport::window_of(N, N / 2, N / 2, 2, 1));
}

/** @brief Tests that a standalone Buffer is seeded on the host, used in a graph task through its data pointer and read back. */
TEST_F(Medium_FluentAPI, BufferAllocateUseAndReadBack)
{
    constexpr std::size_t N = 256;
    auto src = runtime().buffer<float>(N).value();
    auto dst = runtime().buffer<float>(N).value();
    TestSupport::assign_from(src, TestSupport::ramp<float>(N));
    TestSupport::fill_with(dst, 0.0f);
    float* sp = src.data();
    float* dp = dst.data();
    auto g = runtime().graph();
    g.add(N, Reads(BufferRegion(sp)), Writes(BufferRegion(dp)),
          [sp, dp](std::size_t i) { dp[i] = sp[i] * 2.0f; });

    g.run().value();

    std::vector<float> expected = TestSupport::ramp<float>(N);
    for (float& value : expected)
        value *= 2.0f;
    EXPECT_EQ(TestSupport::contents(dst), expected);
}

/** @brief Tests that moving a Buffer leaves the sender empty and the receiver holding the allocation. */
TEST_F(Medium_FluentAPI, BufferMoveLeavesSenderEmpty)
{
    auto buf = runtime().buffer<int>(16).value();

    auto moved = std::move(buf);

    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(moved.empty());
    EXPECT_EQ(moved.size(), 16u);
}

/** @brief Fixture of the Graph::size() tests; the parameter is how many rules the graph records. */
class Medium_FluentGraphSize : public TestSupport::RuntimeTest,
                               public ::testing::WithParamInterface<std::size_t>
{
};

/** @brief Tests that Graph::size() equals the number of add() calls made so far. */
TEST_P(Medium_FluentGraphSize, GraphSizeTracksAdds)
{
    auto f = runtime().state<int>(4).value();
    auto g = runtime().graph();
    record_noop_rules(g, f, GetParam());

    EXPECT_EQ(g.size(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Adds, Medium_FluentGraphSize, ::testing::Values(0u, 1u, 2u));

/** @brief Tests that run(0) returns immediately and reports no work. */
TEST_F(Medium_FluentAPI, RunZeroStepsIsNoOp)
{
    constexpr std::size_t N = 8;
    auto field = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 42);
    auto g = runtime().graph();
    record_increment(g, field, 1);

    auto report = g.run(0).value();

    EXPECT_EQ(report.total_tasks_executed, 0u);
    EXPECT_EQ(TestSupport::contents(field), std::vector<int>(N, 42)) << "run(0) must not execute any steps.";
}

/** @brief Fixture of the multidimensional, variadic, wrapper and captured-argument tests. */
class Medium_FluentHardening : public TestSupport::RuntimeTest
{
};

/**
 * @brief Tests that add_untracked(Extent<2>, kernel) iterates the whole box and writes a row-major
 *        index.
 */
TEST_F(Medium_FluentHardening, Extent2DPerElementMap)
{
    constexpr std::size_t W = 8, H = 6;
    auto buf = runtime().buffer<int>(W * H).value();
    TestSupport::fill_with(buf, -1);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add_untracked(Extent{W, H}, [p](sycl::id<2> idx)
    {
        const std::size_t x = idx[0];
        const std::size_t y = idx[1];
        p[y * W + x] = static_cast<int>(y * W + x);
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(W * H));
}

/**
 * @brief Tests that add_untracked(NdRange<2>, kernel) runs a tiled kernel that writes its
 *        global linear index.
 */
TEST_F(Medium_FluentHardening, NdRange2DTiledKernel)
{
    constexpr std::size_t W = 8, H = 8;   // tiles by 4x4
    auto buf = runtime().buffer<int>(W * H).value();
    TestSupport::fill_with(buf, -1);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add_untracked(NdRange{ sycl::range<2>(W, H), sycl::range<2>(4, 4) }, [p](sycl::nd_item<2> it)
    {
        const std::size_t x = it.get_global_id(0);
        const std::size_t y = it.get_global_id(1);
        p[y * W + x] = static_cast<int>(y * W + x);
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), TestSupport::ramp<int>(W * H));
}

/** @brief Tests that add_rule(Extent<2>, state, kernel) leaves S*y in row y after S steps. */
TEST_F(Medium_FluentHardening, State2DSteppedUpdate)
{
    constexpr std::size_t W = 4, H = 4, S = 3;
    auto field = runtime().state<int>(W * H).value();
    TestSupport::fill_with(field, 0);
    auto g = runtime().graph();
    g.add_rule(Extent{W, H}, field, [](sycl::id<2> idx, const int* cur, int* next)
    {
        const std::size_t x = idx[0];
        const std::size_t y = idx[1];
        next[y * W + x] = cur[y * W + x] + static_cast<int>(y);
    });

    g.run(S).value();

    EXPECT_EQ(TestSupport::contents(field), row_index_times(W, H, static_cast<int>(S)));
}

/**
 * @brief Tests that add(out, in0, in1, kernel) computes next = cur + a + b from previous-step
 * data.
 */
TEST_F(Medium_FluentHardening, VariadicCoupledThreeInputs)
{
    constexpr std::size_t N = 32;
    auto u = runtime().state<int>(N).value();
    auto a = runtime().state<int>(N).value();
    auto b = runtime().state<int>(N).value();
    TestSupport::fill_with(u, 0);
    TestSupport::fill_with(a, 2);
    TestSupport::fill_with(b, 3);
    auto g = runtime().graph();
    // a and b have their own (identity) update rules so they advance each step.
    record_increment(g, a, 0);
    record_increment(g, b, 0);
    g.add_coupled(Coupled(u, a, b), [](std::size_t i, const int* cur, int* next,
                  const int* av, const int* bv)
    {
        next[i] = cur[i] + av[i] + bv[i];
    });

    g.run(4).value();

    EXPECT_EQ(TestSupport::contents(u), std::vector<int>(N, 4 * (2 + 3)));
}

/** @brief Tests that the Reads and Writes wrappers order a writer before a dependent reader. */
TEST_F(Medium_FluentHardening, ReadsWritesWrappersOrdering)
{
    constexpr std::size_t N = 32;
    auto buf = runtime().buffer<int>(N).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    g.add(N, Reads{}, Writes{ buf }, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(N, Reads{ buf }, Writes{ buf }, [p](std::size_t i) { p[i] = p[i] * p[i]; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), squares(N));
}

/**
 * @brief Tests that In and Out captured arguments hand the kernel raw pointers and register the
 *        dependency.
 */
TEST_F(Medium_FluentHardening, InOutCapturedArguments)
{
    constexpr std::size_t N = 64;
    auto src = runtime().buffer<int>(N).value();
    auto dst = runtime().buffer<int>(N).value();
    TestSupport::assign_from(src, TestSupport::ramp<int>(N));
    TestSupport::fill_with(dst, 0);
    auto g = runtime().graph();
    g.add(Extent{N}, In(src), Out(dst),
          [](sycl::id<1> i, const int* s, int* d) { d[i[0]] = s[i[0]] * 2; });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(dst), ramp_times(N, 2));
}

/** @brief Tests that an interior-only update leaves the one-cell border untouched. */
TEST_F(Medium_FluentHardening, InteriorOnlyHaloUpdate)
{
    constexpr std::size_t W = 6, H = 6;
    auto buf = runtime().buffer<int>(W * H).value();
    TestSupport::fill_with(buf, 0);
    int* p = buf.data();
    auto g = runtime().graph();
    // Iterate the (W-2)x(H-2) interior box starting at (1,1); the border is skipped.
    Extent<2> interior{ sycl::range<2>(W - 2, H - 2), sycl::id<2>(1, 1) };
    g.add(interior, Reads{ buf }, Writes{ buf }, [p](sycl::id<2> idx)
    {
        p[idx[1] * W + idx[0]] += 1;
    });

    g.run().value();

    EXPECT_EQ(TestSupport::contents(buf), bordered_grid(W, H, 1, 0));
}

/** @brief Fixture of the State replay, run_until, coupled field, recompilation and error tests. */
class Medium_FluentLifecycle : public TestSupport::RuntimeTest
{
};

/**
 * @brief Tests state replay and double-buffering during fluent execution.
 *
 * One update rule increments every element by 1 each step. Verifies that the compiled plan is properly replayed and State is double-buffered so each step reads the previous field and writes the next one.
 */
TEST_F(Medium_FluentLifecycle, StateReplayIncrement)
{
    constexpr std::size_t N     = 1024;
    constexpr std::size_t STEPS = 64;
    auto field = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 0);
    auto sim = runtime().graph();
    record_increment(sim, field, 1);

    auto report = sim.run(STEPS).value();

    EXPECT_EQ(report.total_tasks_executed, STEPS); // one task replayed STEPS times
    EXPECT_EQ(TestSupport::contents(field), std::vector<int>(N, static_cast<int>(STEPS)))
        << "State replay/double-buffer produced wrong values.";
}

/**
 * @brief Tests replay across multiple run() calls.
 *
 * Verifies that the second run resets the node state the first run consumed, ensuring continuous execution.
 */
TEST_F(Medium_FluentLifecycle, ReplayAcrossCalls)
{
    constexpr std::size_t N = 256;
    auto field = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 0);
    auto sim = runtime().graph();
    record_increment(sim, field, 1);
    sim.run(10).value();

    sim.run(5).value();

    EXPECT_EQ(TestSupport::contents(field), std::vector<int>(N, 15));
}

/**
 * @brief Tests that run_until stops exactly when the condition is met.
 *
 * Checks its stop condition before each step using the State the previous step produced.
 */
TEST_F(Medium_FluentLifecycle, RunUntilStopsOnCondition)
{
    constexpr std::size_t N = 64;
    auto field = runtime().state<int>(N).value();
    TestSupport::fill_with(field, 0);
    auto sim = runtime().graph();
    record_increment(sim, field, 1);

    sim.run_until([&]() { return field[0] >= 20; }).value();

    EXPECT_EQ(field[0], 20);
}

/**
 * @brief Tests that coupled fields evolve using Jacobi-style updates.
 *
 * Both rules read the previous step's data, verifying the advance-after-step rule rather than per-rule swapping.
 */
TEST_F(Medium_FluentLifecycle, CoupledFieldsAreJacobi)
{
    constexpr std::size_t N     = 128;
    constexpr std::size_t STEPS = 10;
    auto u = runtime().state<int>(N).value();
    auto v = runtime().state<int>(N).value();
    TestSupport::fill_with(u, 0);
    TestSupport::fill_with(v, 0);
    auto sim = runtime().graph();
    record_increment(sim, v, 1);
    sim.add_coupled(Coupled(u, v), [](std::size_t i, const int* uc, int* un, const int* vc)
    {
        un[i] = uc[i] + vc[i];
    });

    sim.run(STEPS).value();

    EXPECT_EQ(TestSupport::contents(v), std::vector<int>(N, static_cast<int>(STEPS)));
    EXPECT_EQ(TestSupport::contents(u), std::vector<int>(N, static_cast<int>(STEPS * (STEPS - 1) / 2)));
}

/**
 * @brief Tests that mutating the graph recompiles the plan.
 *
 * The next run recompiles with the added rule, and existing state continues from where it left off.
 */
TEST_F(Medium_FluentLifecycle, MutationRecompiles)
{
    constexpr std::size_t N = 128;
    auto a = runtime().state<int>(N).value();
    auto b = runtime().state<int>(N).value();
    TestSupport::fill_with(a, 0);
    TestSupport::fill_with(b, 0);
    auto sim = runtime().graph();
    record_increment(sim, a, 1);
    sim.run(3).value(); // a == 3
    record_increment(sim, b, 2);

    sim.run(2).value(); // recompiles; a += 2 -> 5, b == 4

    EXPECT_EQ(TestSupport::contents(a), std::vector<int>(N, 5));
    EXPECT_EQ(TestSupport::contents(b), std::vector<int>(N, 4));
}

/**
 * @brief Tests that an empty graph is reported as invalid.
 *
 * The non-throwing run() channel reports an InvalidGraph error without throwing an exception.
 */
TEST_F(Medium_FluentLifecycle, EmptyGraphIsInvalid)
{
    auto sim = runtime().graph();

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
TEST_F(Medium_FluentLifecycle, DanglingResourceIsReported)
{
    constexpr std::size_t N = 64;
    auto sim = runtime().graph();
    {
        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();
        sim.add(N, Reads(), Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    }   // buf is destroyed here, before the graph runs

    auto r = sim.run();

    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, ErrorCode::DanglingResource);
}
