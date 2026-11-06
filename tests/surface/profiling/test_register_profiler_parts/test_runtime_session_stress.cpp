/**************************************************************************/
/* test_runtime_session_stress.cpp                                        */
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
 * @file test_runtime_session_stress.cpp
 * @brief Tests two runtimes feeding one source from several threads while the consumer collects.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/config/profiler_config.hpp>
#include <SushiProfiler/json/json_reader.hpp>
#include <SushiProfiler/periods/period_timeline.hpp>
#include <SushiProfiler/record/run_record.hpp>
#include <SushiProfiler/registry/registry.hpp>
#include <SushiProfiler/session/session.hpp>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/profiling/register_profiler_parts.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;

namespace
{
    constexpr std::size_t K_RUNS_PER_THREAD = 40;
    constexpr std::size_t K_THREADS_PER_RUNTIME = 1;
    constexpr std::uint32_t K_MARKS = 12;

    /** @brief Owns one graph and the buffer it writes. */
    struct StressGraph
    {
        /** @brief Records one node into a graph of @p runtime. */
        explicit StressGraph(SushiRuntime::Runtime& runtime)
            : buffer(runtime.buffer<float>(64).value()), graph(runtime.graph())
        {
            float* data = buffer.data();
            NodeOptions options;
            options.name = "stress";
            graph.add(std::size_t{64}, Reads{}, Writes(buffer),
                      [data](std::size_t i) { data[i] = static_cast<float>(i); }, options);
        }

        StressGraph(const StressGraph&) = delete;
        StressGraph& operator=(const StressGraph&) = delete;

        SushiRuntime::Buffer<float> buffer; /**< What the node writes. */
        SushiRuntime::Graph graph;          /**< The recorded node. */
    };

    /** @brief Makes one graph per thread for each of the two runtimes; a runtime refuses a second run while one is in flight. */
    std::vector<std::unique_ptr<StressGraph>> graphs_for(SushiRuntime::Runtime& first, SushiRuntime::Runtime& second)
    {
        std::vector<std::unique_ptr<StressGraph>> graphs;
        for (std::size_t thread = 0; thread < K_THREADS_PER_RUNTIME; ++thread)
        {
            graphs.push_back(std::make_unique<StressGraph>(first));
            graphs.push_back(std::make_unique<StressGraph>(second));
        }
        return graphs;
    }

    /** @brief Runs each graph on its own thread while another thread marks @p timeline; returns how many runs failed. */
    std::size_t run_all_while_marking(std::vector<std::unique_ptr<StressGraph>>& graphs,
                                      SushiProfiler::PeriodTimeline& timeline)
    {
        std::atomic<std::size_t> failures{0};
        std::atomic<bool> running{true};
        std::vector<std::thread> workers;
        for (const std::unique_ptr<StressGraph>& stress : graphs)
            workers.emplace_back([&failures, &stress]()
            {
                for (std::size_t run = 0; run < K_RUNS_PER_THREAD; ++run)
                    if (!stress->graph.run().has_value())
                        failures.fetch_add(1);
            });
        std::thread marker([&running, &timeline]()
        {
            for (std::uint32_t mark = 0; mark < K_MARKS && running.load(std::memory_order_acquire); ++mark)
            {
                timeline.mark();
                std::this_thread::yield();
            }
        });
        for (std::thread& worker : workers)
            worker.join();
        running.store(false, std::memory_order_release);
        marker.join();
        timeline.mark();
        return failures.load();
    }

    /** @brief Expects every span of @p record to be well formed and on one of @p allowed tracks or a host track. */
    void expect_every_span_well_formed(const SushiProfiler::RunRecord& record, const std::set<std::string>& allowed)
    {
        for (const SushiProfiler::PeriodRecord& period : record.periods)
            for (std::size_t row = 0; row < period.spans.size(); ++row)
            {
                const SushiProfiler::Span span = period.spans.span(row);
                const std::string& track = record.tracks.at(span.track);
                EXPECT_LE(span.queued_ns, span.start_ns);
                EXPECT_LE(span.start_ns, span.end_ns);
                EXPECT_TRUE(allowed.count(track) != 0 || track.find("/host") != std::string::npos) << track;
            }
    }
} // namespace

/**
 * @brief Tests that two runtimes feeding one source from several threads while the consumer drains fail no run
 *        and leave only well-formed spans on the runtimes' tracks.
 *
 * A runtime runs one graph at a time, so each of the two runtimes has one thread that runs its own graph; their
 * worker pools, the marking thread and the consumer make the rest. The marking thread marks the timeline the window
 * follows, so periods close and are collected while the runs keep arriving.
 */
TEST(Medium_RuntimeSessionStress, TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects)
{
    SessionPipeline first_runtime;
    SessionPipeline second_runtime;
    auto graphs = graphs_for(first_runtime.runtime(), second_runtime.runtime());
    SushiProfiler::Registry registry;
    RuntimeProfilerParts parts = register_profiler_parts(registry, first_runtime.runtime());
    ASSERT_TRUE(parts.attach(second_runtime.runtime()));
    SushiProfiler::SessionStart start =
        SushiProfiler::Session::start(session_config(1, K_MARKS - 1), registry);
    ASSERT_TRUE(start.ok()) << start.error;

    const std::size_t failures = run_all_while_marking(graphs, start.session->timeline("run.step"));
    const SushiProfiler::SessionStop stop = start.session->stop();

    EXPECT_EQ(failures, 0u) << "profiling must not fail a run";
    EXPECT_TRUE(stop.problems.empty());
    expect_every_span_well_formed(stop.record, {device_track_name(first_runtime.runtime(), 0),
                                                device_track_name(second_runtime.runtime(), 0)});
}
