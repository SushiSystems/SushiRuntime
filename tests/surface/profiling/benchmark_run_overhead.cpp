/**************************************************************************/
/* benchmark_run_overhead.cpp                                             */
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
 * @file benchmark_run_overhead.cpp
 * @brief Measures what the profiler costs one run of a graph with the gate closed, open and observed.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/environment/environment_contributor.hpp>
#include <SushiProfiler/record/period_record.hpp>
#include <SushiProfiler/registry/registry.hpp>
#include <SushiProfiler/session/session.hpp>

#include <SushiRuntime/measurement/measurement.hpp>
#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "test_register_profiler_parts/session_pipeline.hpp"

using namespace SushiRuntime;
using SushiRuntime::Test::RunStatistics;

namespace
{
    constexpr std::size_t K_RUNS_PER_SAMPLE = 200;
    constexpr std::size_t K_SAMPLES = 15;
    constexpr std::size_t K_WARMUP_RUNS = 20;

    /** @brief Returns the nanoseconds one run of @p graph takes, as the mean of K_RUNS_PER_SAMPLE runs. */
    double nanoseconds_per_run(Graph& graph)
    {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t run = 0; run < K_RUNS_PER_SAMPLE; ++run)
            if (!graph.run().has_value())
                return 0.0;
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(K_RUNS_PER_SAMPLE);
    }

    /** @brief Runs the graph a few times so its plan is built and the caches are warm. */
    void warm(Graph& graph)
    {
        for (std::size_t run = 0; run < K_WARMUP_RUNS; ++run)
            EXPECT_TRUE(graph.run().has_value());
    }

    /** @brief Prints one row of the table. */
    void print_row(const char* state, const RunStatistics& statistics, double baseline_ns)
    {
        std::printf("  %-34s | %10.1f | %10.1f | %10.1f | %+9.1f\n", state, statistics.median, statistics.minimum,
                    statistics.maximum, statistics.median - baseline_ns);
    }
} // namespace

/**
 * @brief Measures the per-run cost of the zones at the level this library was compiled at, for four states.
 *
 * Closed: no session, so every zone is one relaxed load and a branch. Open: a session with no source, so zones
 * and values are written. Observed: an observer is added, so plans record device timing, with the gate closed.
 * Observed and open: a session with the device source. The level is a build setting; run the program from a build
 * at each level to compare them.
 */
TEST(Medium_RunOverheadBenchmark, PrintsTheCostOfOneRunInEachState)
{
    SessionTests::SessionPipeline pipeline;
    Graph& graph = pipeline.graph();
    warm(graph);

    SushiProfiler::Collector::instance().close();
    const RunStatistics closed = Test::measure([&graph]() { return nanoseconds_per_run(graph); }, K_SAMPLES);

    RunStatistics open;
    {
        const SushiProfiler::Registry registry;
        SushiProfiler::ProfilerConfig config = SessionTests::session_config(0, 1);
        config.sources.clear();
        SushiProfiler::SessionStart start = SushiProfiler::Session::start(config, registry);
        ASSERT_TRUE(start.ok()) << start.error;
        start.session->timeline("run.step").mark();
        open = Test::measure([&graph]() { return nanoseconds_per_run(graph); }, K_SAMPLES);
        start.session->timeline("run.step").mark();
        static_cast<void>(start.session->stop());
    }

    RunStatistics observed;
    {
        const auto observer = std::make_shared<TestRunner::RecordingObserver>();
        const RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);
        warm(graph);
        observed = Test::measure([&graph]() { return nanoseconds_per_run(graph); }, K_SAMPLES);
    }

    RunStatistics both;
    {
        SushiProfiler::Registry registry;
        RuntimeProfilerParts parts = register_profiler_parts(registry, pipeline.runtime());
        SushiProfiler::SessionStart start = SushiProfiler::Session::start(SessionTests::session_config(0, 1), registry);
        ASSERT_TRUE(start.ok()) << start.error;
        start.session->timeline("run.step").mark();
        warm(graph);
        both = Test::measure([&graph]() { return nanoseconds_per_run(graph); }, K_SAMPLES);
        start.session->timeline("run.step").mark();
        static_cast<void>(start.session->stop());
    }

    std::printf("\n=== Profiler cost of one run of 3 nodes (%zu runs a sample, median of %zu samples, %s build) ===\n",
                K_RUNS_PER_SAMPLE, K_SAMPLES, Test::build_type());
    std::printf("  %-34s | %10s | %10s | %10s | %9s\n", "state (ns per run)", "median", "min", "max", "vs closed");
    print_row("gate closed", closed, closed.median);
    print_row("gate open, zones and values", open, closed.median);
    print_row("observer added, device timing", observed, closed.median);
    print_row("session with the device source", both, closed.median);

    EXPECT_GT(closed.median, 0.0);
    EXPECT_GT(open.median, 0.0);
    EXPECT_GT(observed.median, 0.0);
    EXPECT_GT(both.median, 0.0);
}
