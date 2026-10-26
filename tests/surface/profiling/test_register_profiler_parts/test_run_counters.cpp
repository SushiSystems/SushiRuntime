/**************************************************************************/
/* test_run_counters.cpp                                                  */
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
 * @file test_run_counters.cpp
 * @brief Tests the counters and values the runtime writes at the end of every run.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::RunRecord;

namespace
{
    constexpr std::size_t K_WRITTEN = 100;
    constexpr std::size_t K_READ = 40;
} // namespace

/** @brief Tests that a run that copies a known number of bytes writes counters of exactly those bytes. */
TEST(Medium_RunCounters, TransferBytesMatchTheExplicitCopies)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<float>(256, DeviceIndex{0}, Residency::Device).value();
    const std::vector<float> source(K_WRITTEN, 1.0f);
    std::vector<float> sink;
    auto graph = runtime.graph();
    graph.add_host(Reads{}, Writes(buffer), [&buffer, &source]()
    {
        EXPECT_TRUE(buffer.write_range(ElementRange{0, K_WRITTEN}, span<const float>(source.data(), source.size())).has_value());
    });
    graph.add_host(Reads(buffer), Writes{}, [&buffer, &sink]()
    {
        auto read = buffer.read_range(ElementRange{0, K_READ});
        EXPECT_TRUE(read.has_value());
        if (read.has_value())
            sink = *read;
    });

    const RunRecord record = record_periods_without_source(
        0, 1, [&graph](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(graph.run().has_value()); });

    EXPECT_EQ(sample_total(record, "transfer.h2d_bytes").first, static_cast<double>(K_WRITTEN * sizeof(float)));
    EXPECT_EQ(sample_total(record, "transfer.d2h_bytes").first, static_cast<double>(K_READ * sizeof(float)));
}

/** @brief Tests that a run that copies nothing writes counters of zero, not no counter. */
TEST(Medium_RunCounters, ARunThatCopiesNothingWritesZeroBytes)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 2, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(pipeline.graph().run().has_value()); });

    EXPECT_EQ(sample_total(record, "transfer.h2d_bytes").first, 0.0);
    EXPECT_EQ(sample_total(record, "transfer.h2d_bytes").second, 2u) << "one counter per run";
}

/** @brief Tests that the task count of every run is written as a counter. */
TEST(Medium_RunCounters, TheTaskCountIsWrittenPerRun)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 3, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(pipeline.graph().run().has_value()); });

    const auto tasks = sample_total(record, "run.tasks");
    EXPECT_EQ(tasks.second, 3u);
    EXPECT_EQ(tasks.first, 9.0) << "three nodes in each of three runs";
}

/** @brief Tests that the worker time is written as four values on every run, with no observer added. */
TEST(Medium_RunCounters, WorkerBucketSumsAreWrittenOnEveryRun)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 3, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(pipeline.graph().run().has_value()); });

    for (const char* name : {"worker.busy", "worker.stealing", "worker.polling", "worker.idle"})
    {
        const auto total = sample_total(record, name);
        EXPECT_EQ(total.second, 3u) << name << ": one value per run";
        EXPECT_GE(total.first, 0.0) << name;
    }
}

/** @brief Tests that the worker values of a run that recorded no timing are zero, not the time of an earlier run. */
TEST(Medium_RunCounters, AnUnobservedRunReportsNoStaleWorkerTime)
{
    SessionPipeline pipeline;
    const auto observer = std::make_shared<TestRunner::RecordingObserver>();
    RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    const RunRecord record = record_periods_without_source(
        0, 2, [&pipeline, &handle](std::uint32_t period, SushiProfiler::PeriodTimeline&)
        {
            // The first run records timing and so accumulates worker time; the second has no observer.
            if (period == 1)
                handle.reset();
            EXPECT_TRUE(pipeline.graph().run().has_value());
        });

    ASSERT_EQ(record.periods.size(), 2u);
    double busy_in_second_period = -1.0;
    for (const SushiProfiler::SampleSeries& series : record.periods[1].samples)
        if (record.sites[series.site].name == "worker.busy")
            for (const double value : series.values)
                busy_in_second_period = value;
    EXPECT_EQ(busy_in_second_period, 0.0) << "a run that measured nothing writes zero, not what the run before it measured";
}
