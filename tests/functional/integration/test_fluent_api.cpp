/**************************************************************************/
/* test_fluent_api.cpp                                                    */
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

// Coverage for fluent API surface not exercised by test_fluent_lifecycle.cpp:
//   - run_for(duration)
//   - try_run() success path and RunReport field validation
//   - add(fn) raw sycl::handler overload
//   - add(n, fn) range kernel overload
//   - add(reads, writes, n, fn) pointer-tracked overload
//   - Reads/Writes over Buffer::region() sub-region tracking
//   - Buffer<T> standalone allocation and graph use
//   - Graph::size()
//   - Runtime::advanced() / unsafe_context() accessors
//   - run(0) edge case

#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// try_run() on a non-empty graph must return a successful result with a
// populated total_tasks_executed. This proves the non-throwing path returns
// a valid RunReport rather than a default-constructed one.
TEST(Integration_FluentAPI, TryRunSuccessPath)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p   = buf.data();
    void* wk = buf.dependency_key();

    auto g = rt.graph();
    g.add(std::vector<void*>{}, std::vector<void*>{ wk }, N,
          [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto result = g.try_run();
    ASSERT_TRUE(result.has_value()) << "try_run() should succeed on a valid graph.";
    EXPECT_EQ(result->total_tasks_executed, 1u);
    EXPECT_TRUE(result->is_successful);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
}

// RunReport fields after a multi-step run: total_tasks_executed must equal
// tasks-per-step * steps; total_duration_ms must be non-negative.
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

// run_for(duration) must run at least one step and then stop. A very short
// budget is used so the test does not wait; the one-step guarantee is what we
// validate.
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

// run_for with a real budget: multiple steps should complete and the report
// must reflect them. This measures steady-state throughput, not cold start.
TEST(Integration_FluentAPI, RunForRunsMultipleStepsWithinBudget)
{
    constexpr std::size_t N = 8;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Warm the kernel before timing. The first dispatch pays one-off JIT
    // compilation and queue/device setup that can alone exceed the 50ms budget;
    // run_for fixes the deadline before its first step, so an unwarmed kernel
    // overshoots and stops after a single step. The steady-state cost of this
    // trivial kernel is sub-millisecond, so a warmed run fits many steps.
    g.run();

    auto report = g.run_for(std::chrono::milliseconds(50));

    EXPECT_GE(report.total_tasks_executed, 2u)
        << "run_for(50ms) should complete more than one step for a trivial kernel.";
    EXPECT_GE(report.total_duration_ms, 0.0);
}

// add_untracked(fn) where fn takes sycl::handler& directly (the raw kernel
// overload). The kernel must run and produce the expected values.
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

// add_untracked(n, fn) where fn receives (begin, end, sycl::handler&) — the
// range kernel overload. The fn must see begin=0 and end=N and launch its own work.
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

// add(reads, writes, n, fn) — explicit dependency-vector overload. Two tasks
// sharing one buffer: a writer followed by a reader that squares the values.
// The dependency tracker must order them correctly without a State.
TEST(Integration_FluentAPI, PointerTrackedAddOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();
    void* key = buf.dependency_key();

    auto g = rt.graph();

    // Task 1: write p[i] = i
    g.add({}, { key }, N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    // Task 2: square in place (RAW on p -> must run after task 1)
    g.add({ key }, { key }, N, [p](std::size_t i) { p[i] = p[i] * p[i]; });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

// The tracked range-kernel overload: the same writer/squarer pair as
// PointerTrackedAddOrdering, but the second task drives the handler itself. It
// must still be ordered after the writer by the declared Reads/Writes; an
// untracked overload here would let the square race the write.
TEST(Integration_FluentAPI, TrackedRangeKernelOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(Reads(), Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(Reads(buf), Writes(buf), N,
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

// The tracked raw-kernel overload: the reader owns the whole command group and
// is still ordered after the writer by its declared regions.
TEST(Integration_FluentAPI, TrackedRawKernelOrdering)
{
    constexpr std::size_t N = 32;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(Reads(), Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
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

// The tracked NdRange overload: a tiled work-group kernel no longer has to give
// up dependency tracking to get an nd_item. Ordered after the writer, and the
// nd_item indexing must still cover the whole global range.
TEST(Integration_FluentAPI, TrackedNdRangeOrdering)
{
    constexpr std::size_t N = 64;   // tiles by 16

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    auto g = rt.graph();
    g.add(Reads(), Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    g.add(Reads(buf), Writes(buf),
          NdRange{ sycl::range<1>(N), sycl::range<1>(16) },
          [p](sycl::nd_item<1> it)
    {
        const std::size_t i = it.get_global_id(0);
        p[i] = p[i] * p[i];
    });

    g.run();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i * i)) << "at i=" << i;
}

// Region-tracked add: two writers touch disjoint halves of one buffer, so the
// tracker leaves them unordered, then a whole-buffer reader overlaps both and
// must run after each. Proves sub-region identity end-to-end through the fluent
// layer: disjoint slices are independent while an overlapping access still
// orders. A broken (base-pointer) identity would either serialize the halves or
// let the reader observe an unwritten half.
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
    g.add(Reads(), Writes(buf.region({0, N / 2})), N / 2,
          [p](std::size_t i) { p[i] = 1; });
    g.add(Reads(), Writes(buf.region({N / 2, N / 2})), N / 2,
          [p](std::size_t i) { p[N / 2 + i] = 2; });

    // Whole-buffer read overlaps both halves -> ordered after both writers.
    g.add(Reads(buf), Writes(out), N,
          [p, o](std::size_t i) { o[i] = p[i]; });

    g.run();

    for (std::size_t i = 0; i < N / 2; ++i)
        ASSERT_EQ(out[i], 1) << "lower half at i=" << i;
    for (std::size_t i = N / 2; i < N; ++i)
        ASSERT_EQ(out[i], 2) << "upper half at i=" << i;
}

// Buffer<T> standalone: allocate, seed host-side, use in a graph task via
// dependency_key(), and read back. Verifies the owning-USM lifecycle.
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
    g.add({ src.dependency_key() }, { dst.dependency_key() }, N,
          [sp, dp](std::size_t i) { dp[i] = sp[i] * 2.0f; });

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

// Graph::size() tracks the number of add() calls made so far, including after
// a mutation that would trigger recompilation.
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

// Runtime::unsafe_context() must return a reference to the same RuntimeContext
// the fluent Runtime wraps, and advanced() must read through to it. Verified by
// comparing device counts.
TEST(Integration_FluentAPI, ContextAccessorIsConsistent)
{
    auto rt = Runtime::create();
    Execution::RuntimeContext& ctx = rt.unsafe_context();

    // device_count() should be > 0 (at least a CPU device is always present).
    EXPECT_GE(ctx.device_count(), 1u);
    EXPECT_EQ(rt.advanced().device_count(), ctx.device_count());
}

// run(0) is documented as "zero steps" — it must return immediately and the
// RunReport should reflect no work done.
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
