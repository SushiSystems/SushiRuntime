/**************************************************************************/
/* test_run_report_reuse.cpp                                              */
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
 * @file test_run_report_reuse.cpp
 * @brief Tests the caller-owned RunReport overloads of run(), try_run() and RunHandle::wait().
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N = 64;

    /**
     * @brief Zeroes the buffer and records the one task every test here runs, which writes i+1 to
     *        element i.
     */
    void record_write(API::Graph& g, API::Buffer<int>& buf)
    {
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

        int* p = buf.data();
        g.add(API::Reads{}, API::Writes(buf), N,
              [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    }
}

/** @brief Tests that the reuse overload reports what the value-returning one reports. */
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

/** @brief Tests that steady-state runs refill the report's vectors in place. */
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

/** @brief Tests that a short run after a long one reports the short run's count. */
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

/** @brief Tests that a rejected run clears the report and marks it unsuccessful. */
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

/** @brief Tests that try_run(RunReport&) returns the report it was given. */
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

/** @brief Tests that RunHandle::wait(RunReport&) fills the caller's report. */
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
