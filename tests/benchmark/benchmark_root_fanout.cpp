/**************************************************************************/
/* benchmark_root_fanout.cpp                                              */
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
 * @file benchmark_root_fanout.cpp
 * @brief Measures the per-step and per-root cost of replaying a graph of independent roots.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

#include "measurement.hpp"

using namespace SushiRuntime;
using SushiRuntime::Test::MeasurementLabel;
using SushiRuntime::Test::RunStatistics;

namespace
{
    /**
     * @brief Holds what one fan-out case measured across its timed runs.
     */
    struct FanoutMeasurement
    {
        RunStatistics wall_ns_per_root_step;     /**< Timed by this test around Graph::run(). */
        RunStatistics reported_ns_per_root_step; /**< Derived from RunReport::total_duration_ms. */
    };

    /**
     * @brief Replays a graph of @p roots independent tasks @p steps times per timed run.
     * @return The per-root cost by both clocks.
     */
    FanoutMeasurement measure_fanout(std::size_t roots, std::size_t steps)
    {
        auto rt = Runtime::create();
        rt.rebalancer(false);

        std::vector<Buffer<int>> bufs;
        bufs.reserve(roots);
        for (std::size_t r = 0; r < roots; ++r)
            bufs.push_back(rt.buffer<int>(1));
        for (auto& b : bufs) b[0] = 0;

        auto g = rt.graph();
        for (std::size_t r = 0; r < roots; ++r)
        {
            int* p = bufs[r].data();
            // Each task touches only its own buffer -> independent -> a root.
            g.add(API::Reads{}, API::Writes(bufs[r]), 1,
                  [p](std::size_t) { p[0] += 1; });
        }

        // The first run compiles the plan; every timed run below is a replay.
        g.run(std::size_t{1});

        const double root_steps = double(steps) * double(roots);
        std::vector<double> reported_samples;
        reported_samples.reserve(SushiRuntime::Test::K_DEFAULT_RUN_COUNT);

        FanoutMeasurement measurement;
        measurement.wall_ns_per_root_step = SushiRuntime::Test::measure([&]() -> double
        {
            const auto t0 = std::chrono::steady_clock::now();
            const auto report = g.run(steps);
            const auto t1 = std::chrono::steady_clock::now();

            reported_samples.push_back((report.total_duration_ms * 1e6) / root_steps);
            return std::chrono::duration<double, std::nano>(t1 - t0).count() / root_steps;
        });
        const std::size_t timed_runs = measurement.wall_ns_per_root_step.run_count;
        measurement.reported_ns_per_root_step =
            SushiRuntime::Test::summarize(std::move(reported_samples));

        EXPECT_EQ(g.compile_count(), 1u);

        // Every root ran once in the compiling run and `steps` times in each timed run.
        const std::size_t expected_runs = 1 + timed_runs * steps;
        for (std::size_t r = 0; r < roots; ++r)
            EXPECT_EQ(bufs[r][0], static_cast<int>(expected_runs))
                << "root " << r << " did not run every step";

        return measurement;
    }
}

TEST(Performance_RootFanout, InjectionCostVsRootCount)
{
    // Keep step*root work bounded so the sweep stays quick; fewer steps at high R.
    struct Case { std::size_t roots; std::size_t steps; };
    const Case cases[] = {
        {1,    20000},
        {16,   10000},
        {256,  2000},
        {1024, 500},
    };

    // A default-configured runtime starts one worker per hardware thread.
    const std::size_t workers = std::thread::hardware_concurrency();

    std::printf("\n=== Root fan-out injection sweep (batched submit, median of %zu runs) ===\n",
                SushiRuntime::Test::K_DEFAULT_RUN_COUNT);
    std::printf("  %8s | %12s | %16s | %14s\n",
                "roots", "ms/step", "ns/(root*step)", "runtime clock");
    for (const auto& c : cases)
    {
        const FanoutMeasurement m = measure_fanout(c.roots, c.steps);
        const double ms_per_step = m.wall_ns_per_root_step.median * double(c.roots) / 1e6;
        std::printf("  %8zu | %12.5f | %16.1f | %14.1f\n",
                    c.roots, ms_per_step, m.wall_ns_per_root_step.median,
                    m.reported_ns_per_root_step.median);

        MeasurementLabel label;
        label.benchmark    = "root_fanout";
        label.case_name    = "batched_submit";
        label.unit         = "ns_per_root_step";
        label.worker_count = workers;
        label.problem_size = c.roots;
        SushiRuntime::Test::print_measurement(label, m.wall_ns_per_root_step);

        EXPECT_GT(ms_per_step, 0.0);
    }
    std::printf("  -> the per-step injection cost is now one lock + one wake for the\n"
                "     whole root set; ns/(root*step) trending down as R grows means the\n"
                "     injector is no longer scaling with fan-out (ds-1 working).\n");
}
