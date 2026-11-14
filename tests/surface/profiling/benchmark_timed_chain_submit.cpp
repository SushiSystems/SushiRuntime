/**************************************************************************/
/* benchmark_timed_chain_submit.cpp                                       */
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
 * @file benchmark_timed_chain_submit.cpp
 * @brief Measures the host time of one chained submit with device timing on and off.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/measurement/measurement.hpp>
#include <SushiRuntime/report/recording_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "test_register_profiler_parts/profiled_pipelines.hpp"

using namespace SushiRuntime;
using SushiRuntime::Test::RunStatistics;

namespace
{
    constexpr std::size_t K_NODES = 51;
    constexpr std::size_t K_ELEMENTS = 64;
    constexpr std::uint32_t K_RUNS = 200;
    constexpr std::size_t K_WARMUP_RUNS = 20;
    constexpr double K_NANOSECONDS_PER_MICROSECOND = 1000.0;

    /** @brief Holds what one row measured, in microseconds. */
    struct RowStatistics
    {
        RunStatistics submit_per_node; /**< The chain.submit_step zone of a run, divided by the node count. */
        RunStatistics run_wall;        /**< Graph::run() around the whole run, wait and record included. */
        bool on_chain = false;         /**< Whether every run took the chain layer. */
    };

    /**
     * @brief Owns a graph of K_NODES small dependent kernels on the program's default runtime.
     *
     * Neither copied nor moved: the kernels hold the buffers' addresses.
     */
    class ChainedKernels
    {
        public:
            /** @brief Records the nodes on the program's runtime of the default shape, which lets a chainable plan run as a chain. */
            ChainedKernels()
                : runtime_(TestSupport::RuntimeEnvironment::runtime(SessionTests::SessionPipeline::chain_config())),
                  graph_(runtime_.graph())
            {
                buffers_.reserve(K_NODES);
                for (std::size_t node = 0; node < K_NODES; ++node)
                    buffers_.push_back(runtime_.buffer<float>(K_ELEMENTS).value());

                float* first = buffers_[0].data();
                graph_.add(K_ELEMENTS, Reads{}, Writes(buffers_[0]),
                           [first](std::size_t i) { first[i] = static_cast<float>(i); });
                for (std::size_t node = 1; node < K_NODES; ++node)
                {
                    const float* in = buffers_[node - 1].data();
                    float* out = buffers_[node].data();
                    graph_.add(K_ELEMENTS, Reads(buffers_[node - 1]), Writes(buffers_[node]),
                               [in, out](std::size_t i) { out[i] = in[i] + 1.0f; });
                }
            }

            ChainedKernels(const ChainedKernels&) = delete;
            ChainedKernels& operator=(const ChainedKernels&) = delete;
            ChainedKernels(ChainedKernels&&) = delete;
            ChainedKernels& operator=(ChainedKernels&&) = delete;

            /** @brief Returns the runtime. */
            Runtime& runtime() noexcept { return runtime_; }

            /** @brief Returns the graph. */
            Graph& graph() noexcept { return graph_; }

        private:
            Runtime& runtime_;                  /**< The program's runtime of the default shape. */
            std::vector<Buffer<float>> buffers_; /**< One output buffer per node. */
            Graph graph_;                       /**< The recorded nodes. */
    };

    /** @brief Runs the graph a few times so its plan is built and the caches are warm. */
    void warm(Graph& graph)
    {
        for (std::size_t run = 0; run < K_WARMUP_RUNS; ++run)
            EXPECT_TRUE(graph.run().has_value());
    }

    /** @brief Returns the microseconds the chain.submit_step zones of @p record hold, one value per period that has one. */
    std::vector<double> submit_microseconds(const SushiProfiler::RunRecord& record)
    {
        std::vector<double> values;
        for (const SushiProfiler::PeriodRecord& period : record.periods)
            for (const SushiProfiler::ZoneTree& tree : period.zones)
            {
                const int zone = SessionTests::find_zone(record, tree, "chain.submit_step");
                if (zone >= 0)
                    values.push_back(static_cast<double>(tree.nodes[static_cast<std::size_t>(zone)].inclusive_ns) /
                                     K_NANOSECONDS_PER_MICROSECOND);
            }
        return values;
    }

    /**
     * @brief Measures K_RUNS runs of @p graph, one per period, with a session open and no source.
     *
     * The session is open on both rows, so the zone cost is the same on both and only the observer differs.
     */
    RowStatistics measure_row(Graph& graph)
    {
        RowStatistics row;
        row.on_chain = true;
        std::vector<double> wall_microseconds;
        wall_microseconds.reserve(K_RUNS);

        const SushiProfiler::RunRecord record = SessionTests::record_periods_without_source(
            0, K_RUNS,
            [&](std::uint32_t, SushiProfiler::PeriodTimeline&)
            {
                const auto start = std::chrono::steady_clock::now();
                const auto report = graph.run();
                const auto end = std::chrono::steady_clock::now();
                EXPECT_TRUE(report.has_value());
                if (!report.has_value() || report.value().layer != ExecutionLayer::Chain)
                    row.on_chain = false;
                wall_microseconds.push_back(std::chrono::duration<double, std::nano>(end - start).count() /
                                            K_NANOSECONDS_PER_MICROSECOND);
            });

        std::vector<double> submit = submit_microseconds(record);
        for (double& value : submit)
            value /= static_cast<double>(K_NODES);
        row.submit_per_node = SushiRuntime::Test::summarize(std::move(submit));
        row.run_wall = SushiRuntime::Test::summarize(std::move(wall_microseconds));
        return row;
    }

    /** @brief Prints one row as a single line of key=value pairs, opened by the measurement prefix. */
    void print_row(const char* case_name, const RowStatistics& row)
    {
        std::printf("%s benchmark=timed_chain_submit case=%s unit=us nodes=%zu runs=%zu build=%s layer=%s "
                    "submit_us_per_node_median=%.6g submit_us_per_node_min=%.6g submit_us_per_node_max=%.6g "
                    "run_wall_us_median=%.6g run_wall_us_min=%.6g run_wall_us_max=%.6g\n",
                    SushiRuntime::Test::K_MEASUREMENT_PREFIX, case_name, K_NODES, row.run_wall.run_count,
                    SushiRuntime::Test::build_type(), row.on_chain ? "chain" : "other", row.submit_per_node.median,
                    row.submit_per_node.minimum, row.submit_per_node.maximum, row.run_wall.median,
                    row.run_wall.minimum, row.run_wall.maximum);
        std::fflush(stdout);
    }
} // namespace

/**
 * @brief Measures the host microseconds of one chained submit and of one run, without and with an observer.
 *
 * The untimed row has no observer, so the chain submits through the event-less path. The timed row adds an
 * observer, so the run asks for device timing, the queue profiles and every node keeps its event.
 */
TEST(Benchmark_TimedChainSubmit, HostCostOfAChainedSubmitWithAndWithoutDeviceTiming)
{
    ChainedKernels kernels;
    Graph& graph = kernels.graph();
    warm(graph);

    const RowStatistics untimed = measure_row(graph);

    RowStatistics timed;
    {
        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = kernels.runtime().add_run_observer(observer);
        warm(graph);
        timed = measure_row(graph);
    }

    EXPECT_TRUE(untimed.on_chain);
    EXPECT_TRUE(timed.on_chain);
    std::printf("\n=== Host cost of one chained submit of %zu nodes (%u runs a row, microseconds) ===\n", K_NODES,
                K_RUNS);
    print_row("untimed", untimed);
    print_row("timed", timed);
}
