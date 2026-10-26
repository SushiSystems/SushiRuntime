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
} // namespace

/**
 * @brief Tests that the source takes observations from two runtimes on several threads while the consumer drains,
 *        and that no observation is lost within the queue's capacity.
 *
 * A runtime runs one graph at a time, so each of the two runtimes has one thread that runs its own graph; their
 * worker pools, the marking thread and the consumer make the rest. The marking thread marks the timeline the window
 * follows, so periods close and are collected while the runs keep arriving.
 */
TEST(Medium_RuntimeSessionStress, TwoRuntimesFeedOneSourceFromSeveralThreadsWhileTheConsumerCollects)
{
    SessionPipeline first_runtime;
    SessionPipeline second_runtime;
    // One graph per runtime: a runtime refuses a second run while one is in flight.
    std::vector<std::unique_ptr<SushiRuntime::Graph>> graphs;
    std::vector<std::unique_ptr<SushiRuntime::Buffer<float>>> buffers;
    const auto add_graph = [&](SushiRuntime::Runtime& runtime)
    {
        buffers.push_back(std::make_unique<SushiRuntime::Buffer<float>>(runtime.buffer<float>(64).value()));
        float* data = buffers.back()->data();
        graphs.push_back(std::make_unique<SushiRuntime::Graph>(runtime.graph()));
        NodeOptions options;
        options.name = "stress";
        graphs.back()->add(std::size_t{64}, Reads{}, Writes(*buffers.back()),
                           [data](std::size_t i) { data[i] = static_cast<float>(i); }, options);
    };
    for (std::size_t thread = 0; thread < K_THREADS_PER_RUNTIME; ++thread)
    {
        add_graph(first_runtime.runtime());
        add_graph(second_runtime.runtime());
    }

    SushiProfiler::Registry registry;
    RuntimeProfilerParts parts = register_profiler_parts(registry, first_runtime.runtime());
    ASSERT_TRUE(parts.attach(second_runtime.runtime()));
    SushiProfiler::SessionStart start =
        SushiProfiler::Session::start(session_config(1, K_MARKS - 1), registry);
    ASSERT_TRUE(start.ok()) << start.error;
    SushiProfiler::PeriodTimeline& timeline = start.session->timeline("run.step");

    std::atomic<std::size_t> failures{0};
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;
    for (std::size_t index = 0; index < graphs.size(); ++index)
        workers.emplace_back([&, index]()
        {
            for (std::size_t run = 0; run < K_RUNS_PER_THREAD; ++run)
                if (!graphs[index]->run().has_value())
                    failures.fetch_add(1);
        });
    std::thread marker([&]()
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
    const SushiProfiler::SessionStop stop = start.session->stop();

    EXPECT_EQ(failures.load(), 0u) << "profiling must not fail a run";
    EXPECT_TRUE(stop.problems.empty());
    // Whatever the periods held, every span they hold is a well-formed span of one of the two runtimes' tracks.
    const std::set<std::string> allowed{device_track_name(first_runtime.runtime(), 0),
                                        device_track_name(second_runtime.runtime(), 0)};
    for (const SushiProfiler::PeriodRecord& period : stop.record.periods)
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            EXPECT_LE(span.queued_ns, span.start_ns);
            EXPECT_LE(span.start_ns, span.end_ns);
            EXPECT_TRUE(allowed.count(stop.record.tracks.at(span.track)) != 0 ||
                        stop.record.tracks.at(span.track).find("/host") != std::string::npos)
                << stop.record.tracks.at(span.track);
        }
}
