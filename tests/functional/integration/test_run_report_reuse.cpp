/****************************************************************/
/* test_run_report_reuse.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for the caller-owned RunReport overloads: run(RunReport&),
// try_run(RunReport&) and RunHandle::wait(RunReport&).
//
// Two properties are being pinned down, and they pull against each other. The
// point of the overloads is that a frame loop keeps one report alive and the
// per-run vectors stop being reallocated — so the report must NOT be rebuilt
// from scratch. But a report carried across ticks must also never let the
// previous run's numbers survive into this one, which is exactly what "do not
// rebuild" invites. The resolution is reset-then-refill: capacity stays, values
// do not. Both halves are asserted here, including on the failure paths, where
// stale numbers would be most misleading — a caller that ignored the returned
// error would otherwise read a successful-looking report.

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N = 64;

    // Zeroes @p buf and records the one task every test here runs: element i
    // gets i+1. Takes both handles by reference rather than returning them,
    // because a Buffer must not be moved once a graph has recorded it — the
    // graph would keep pointing at the old address.
    void record_write(API::Graph& g, API::Buffer<int>& buf)
    {
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p = buf.data();
        g.add(API::Reads{}, API::Writes(buf), N,
              [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    }
}

// The reuse overload must report what the value-returning one reports. If the
// two drivers ever diverge, every number a consumer reads from the cheap path
// silently becomes a different measurement from the documented one.
TEST(Integration_RunReportReuse, ReuseReportsWhatTheValueFormReports)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    const Core::RunReport allocated = g.run(std::size_t{3});

    Core::RunReport reused;
    g.run(std::size_t{3}, reused);

    EXPECT_TRUE(reused.is_successful);
    EXPECT_EQ(reused.total_tasks_executed, allocated.total_tasks_executed);
    EXPECT_EQ(reused.numa_statistics.size(), allocated.numa_statistics.size());
    EXPECT_EQ(reused.cancelled, allocated.cancelled);
    EXPECT_TRUE(reused.warnings.empty());

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
}

// The whole reason the overload exists: the per-run vectors are refilled in
// place. Comparing the row's address before and after is what actually proves
// no reallocation happened — a capacity check alone would pass even if the
// vector were rebuilt into an identically sized new block.
TEST(Integration_RunReportReuse, SteadyStateRunsDoNotReallocateTheReport)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    Core::RunReport report;
    g.run(report);

    ASSERT_FALSE(report.numa_statistics.empty());
    const void* rows_after_first = report.numa_statistics.data();
    const std::size_t capacity_after_first = report.numa_statistics.capacity();

    for (int tick = 0; tick < 8; ++tick)
    {
        g.run(report);
        ASSERT_EQ(static_cast<const void*>(report.numa_statistics.data()), rows_after_first)
            << "the per-run rows were reallocated on tick " << tick;
        ASSERT_EQ(report.numa_statistics.capacity(), capacity_after_first);
    }

    // Refilled, not appended: eight more runs must not leave eight more rows.
    EXPECT_EQ(report.numa_statistics.size(), 1u);
}

// Reuse must not accumulate. A short run following a long one has to read as
// the short run — the count belongs to this run, not to the report.
TEST(Integration_RunReportReuse, EachRunOverwritesTheLastOne)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    Core::RunReport report;
    g.run(std::size_t{5}, report);
    const std::size_t five_steps = report.total_tasks_executed;
    ASSERT_GT(five_steps, 0u);

    g.run(std::size_t{1}, report);
    EXPECT_LT(report.total_tasks_executed, five_steps)
        << "a one-step run reported the five-step run's task count";
    EXPECT_EQ(report.numa_statistics.size(), 1u);
}

// A rejected run must clear the report it was handed. The dangerous case is a
// caller that checks the returned error only sometimes: reading a stale
// is_successful of true off a run that never happened is worse than reading
// zeroes, so the failure path resets and marks the report unsuccessful.
TEST(Integration_RunReportReuse, ARejectedRunLeavesNoStaleNumbers)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    Core::RunReport report;
    g.run(report);
    ASSERT_TRUE(report.is_successful);
    ASSERT_GT(report.total_tasks_executed, 0u);

    // An empty graph cannot run; the same report goes in again.
    auto empty = rt.graph();
    auto rejected = empty.try_run(report);

    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, Core::Errc::invalid_graph);
    EXPECT_FALSE(report.is_successful);
    EXPECT_EQ(report.total_tasks_executed, 0u);
    EXPECT_TRUE(report.numa_statistics.empty());
}

// The non-throwing form hands back the very report it was given, so a caller
// can keep using the result of the call without holding both.
TEST(Integration_RunReportReuse, TryRunReturnsTheCallersReport)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    Core::RunReport report;
    auto result = g.try_run(report);

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, &report);
    EXPECT_TRUE(report.is_successful);
}

// The asynchronous path settles into a caller-owned report too — the frame loop
// that most wants the overload is the overlapped one.
TEST(Integration_RunReportReuse, AsyncWaitFillsTheCallersReport)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    auto g   = rt.graph();
    record_write(g, buf);

    Core::RunReport report;
    for (int tick = 0; tick < 4; ++tick)
    {
        auto step = g.run_async();
        step.wait(report);

        ASSERT_TRUE(report.is_successful) << "on tick " << tick;
        ASSERT_EQ(report.numa_statistics.size(), 1u) << "on tick " << tick;
    }

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
}

// The composed-plan driver shares the fill path, so it gets the same guarantee.
TEST(Integration_RunReportReuse, DynamicGraphRunsIntoACallersReport)
{
    auto rt = Runtime::create();

    auto a = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        a[i] = 0;
    int* pa = a.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(a), N,
                        [pa](std::size_t i) { pa[i] += 1; });

    Core::RunReport report;
    world.run(report);
    ASSERT_FALSE(report.numa_statistics.empty());
    const void* rows_after_first = report.numa_statistics.data();

    world.run(std::size_t{3}, report);

    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(static_cast<const void*>(report.numa_statistics.data()), rows_after_first);
    EXPECT_EQ(report.numa_statistics.size(), 1u);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(a[i], 4) << "at i=" << i;
}
