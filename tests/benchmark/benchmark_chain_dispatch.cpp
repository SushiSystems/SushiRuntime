/**************************************************************************/
/* benchmark_chain_dispatch.cpp                                           */
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
 * @file benchmark_chain_dispatch.cpp
 * @brief Measures the per-node dispatch cost of a dependency chain across chain lengths.
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
     * @brief Holds what one chain case measured across its timed runs.
     */
    struct ChainMeasurement
    {
        RunStatistics wall_ns_per_node_step;     /**< Timed by this test around Graph::run(). */
        RunStatistics reported_ns_per_node_step; /**< Derived from RunReport::total_duration_ms. */
        Core::ExecutionLayer layer;              /**< Layer the plan ran on. */
    };

    /**
     * @brief Replays a chain of @p length trivial kernel nodes @p steps times per timed run.
     * @param force_scheduler True to keep the plan on the scheduler (layer 0).
     * @return The per-node cost by both clocks, and the layer that ran.
     */
    ChainMeasurement measure_chain(std::size_t length, std::size_t steps, bool force_scheduler)
    {
        API::RuntimeConfig config;
        config.force_scheduler = force_scheduler;
        auto rt = Runtime::create(config);
        rt.set_rebalancer_enabled(false);

        std::vector<Buffer<int>> bufs;
        bufs.reserve(length + 1);
        for (std::size_t i = 0; i <= length; ++i)
            bufs.push_back(rt.buffer<int>(1));
        bufs[0][0] = 0;

        auto g = rt.graph();
        for (std::size_t i = 0; i < length; ++i)
        {
            const int* in = bufs[i].data();
            int* out      = bufs[i + 1].data();
            // Trivial work: the cost under measurement is dispatch, not compute.
            g.add(API::Reads(bufs[i]), API::Writes(bufs[i + 1]), std::size_t{1},
                  [in, out](std::size_t) { out[0] = in[0] + 1; });
        }

        // The first run compiles the plan; every timed run below is a replay.
        Core::ExecutionLayer layer = g.run(std::size_t{1}).layer;

        const double node_steps = double(steps) * double(length);
        std::vector<double> reported_samples;
        reported_samples.reserve(SushiRuntime::Test::K_DEFAULT_RUN_COUNT);

        ChainMeasurement measurement;
        measurement.wall_ns_per_node_step = SushiRuntime::Test::measure([&]() -> double
        {
            const auto t0 = std::chrono::steady_clock::now();
            const auto report = g.run(steps);
            const auto t1 = std::chrono::steady_clock::now();

            layer = report.layer;
            reported_samples.push_back((report.total_duration_ms * 1e6) / node_steps);
            return std::chrono::duration<double, std::nano>(t1 - t0).count() / node_steps;
        });
        measurement.reported_ns_per_node_step =
            SushiRuntime::Test::summarize(std::move(reported_samples));
        measurement.layer = layer;

        EXPECT_EQ(g.compile_count(), 1u);
        EXPECT_EQ(bufs[length][0], static_cast<int>(length))
            << "chain of length " << length << " did not settle to the expected value";

        return measurement;
    }
}

/**
 * @brief Tests Graph::run() per-node wall cost over a chain of dependent kernel nodes.
 *
 * Sweeps the chain length and reports ms/step and ns/node, the baseline the
 * graph-replay design's layers are measured against (design spec §0, §G).
 */
TEST(Performance_ChainDispatch, PerNodeCostVsChainLength)
{
    // Fewer steps at large K keeps the sweep quick while still amortizing setup.
    struct Case { std::size_t length; std::size_t steps; };
    const Case cases[] = {
        {1,    20000},
        {16,   5000},
        {256,  500},
        {1024, 100},
    };

    // A default-configured runtime starts one worker per hardware thread.
    const std::size_t workers = std::thread::hardware_concurrency();

    std::printf("\n=== Chain dispatch cost (dependent-node chain, median of %zu runs) ===\n",
                SushiRuntime::Test::K_DEFAULT_RUN_COUNT);
    std::printf("  %8s | %9s | %12s | %14s | %14s\n",
                "length", "layer", "ms/step", "ns/(node*step)", "runtime clock");
    for (const auto& c : cases)
    {
        for (const bool force_scheduler : { true, false })
        {
            const ChainMeasurement m = measure_chain(c.length, c.steps, force_scheduler);
            const bool on_chain = m.layer == Core::ExecutionLayer::Chain;
            const double ms_per_step =
                m.wall_ns_per_node_step.median * double(c.length) / 1e6;
            std::printf("  %8zu | %9s | %12.5f | %14.1f | %14.1f\n",
                        c.length, on_chain ? "chain" : "scheduler", ms_per_step,
                        m.wall_ns_per_node_step.median, m.reported_ns_per_node_step.median);

            MeasurementLabel label;
            label.benchmark    = "chain_dispatch";
            label.case_name    = force_scheduler ? "scheduler" : (on_chain ? "chain" : "unforced");
            label.unit         = "ns_per_node_step";
            label.worker_count = workers;
            label.problem_size = c.length;
            SushiRuntime::Test::print_measurement(label, m.wall_ns_per_node_step);

            EXPECT_GT(ms_per_step, 0.0);
        }
    }
    std::printf("  -> scheduler rows are layer 0, chain rows layer 1 (design spec §C);\n"
                "     both are compared with the probe's columns in §0.\n");
}
