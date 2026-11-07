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
#include <SushiRuntime/report/recording_observer.hpp>

#include "profiled_pipelines.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::RunRecord;

namespace
{
    constexpr std::size_t K_WRITTEN = 100;
    constexpr std::size_t K_READ = 40;
} // namespace

namespace
{
    /** @brief Reads the first @p count elements of @p buffer through the explicit copy, and returns them. */
    std::vector<float> read_back(Buffer<float>& buffer, std::size_t count)
    {
        auto read = buffer.read_range(ElementRange{0, count});
        EXPECT_TRUE(read.has_value());
        return read.has_value() ? *read : std::vector<float>{};
    }
} // namespace

/** @brief Tests that a run that copies a known number of bytes writes counters of exactly those bytes. */
TEST(Medium_RunCounters, TransferBytesMatchTheExplicitCopies)
{
    Runtime& runtime = TestSupport::RuntimeEnvironment::runtime();
    auto buffer = runtime.buffer<float>(256, DeviceIndex{0}, Residency::Device).value();
    const std::vector<float> source(K_WRITTEN, 1.0f);
    auto graph = runtime.graph();
    graph.add_host(Reads{}, Writes(buffer), [&buffer, &source]()
    {
        EXPECT_TRUE(buffer.write_range(ElementRange{0, K_WRITTEN}, span<const float>(source.data(), source.size())).has_value());
    });
    graph.add_host(Reads(buffer), Writes{}, [&buffer]() { static_cast<void>(read_back(buffer, K_READ)); });

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

namespace
{
    /** @brief Expects counter or value @p name to hold @p samples samples in @p record, none of them negative. */
    void expect_one_value_per_run(const RunRecord& record, const char* name, std::size_t samples)
    {
        SCOPED_TRACE(name);
        const auto total = sample_total(record, name);
        EXPECT_EQ(total.second, samples) << "one value per run";
        EXPECT_GE(total.first, 0.0);
    }

    /** @brief Returns the last value site @p name wrote in @p period, or -1 when it wrote none. */
    double last_value_of(const RunRecord& record, const SushiProfiler::PeriodRecord& period, const char* name)
    {
        double last = -1.0;
        for (const SushiProfiler::SampleSeries& series : period.samples)
            if (record.sites[series.site].name == name && !series.values.empty())
                last = series.values.back();
        return last;
    }

    /** @brief Runs @p pipeline's graph once, dropping @p handle first when @p period is @p first_unobserved. */
    void run_unobserved_from(SessionPipeline& pipeline, RunObserverHandle& handle, std::uint32_t first_unobserved,
                             std::uint32_t period)
    {
        if (period == first_unobserved)
            handle.reset();
        EXPECT_TRUE(pipeline.graph().run().has_value());
    }
} // namespace

/** @brief Tests that the worker time is written as four values on every run, with no observer added. */
TEST(Medium_RunCounters, WorkerBucketSumsAreWrittenOnEveryRun)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 3, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(pipeline.graph().run().has_value()); });

    expect_one_value_per_run(record, "worker.busy", 3u);
    expect_one_value_per_run(record, "worker.stealing", 3u);
    expect_one_value_per_run(record, "worker.polling", 3u);
    expect_one_value_per_run(record, "worker.idle", 3u);
}

/** @brief Tests that the worker values of a run that recorded no timing are zero, not the time of an earlier run. */
TEST(Medium_RunCounters, AnUnobservedRunReportsNoStaleWorkerTime)
{
    SessionPipeline pipeline;
    const auto observer = std::make_shared<RecordingObserver>();
    RunObserverHandle handle = pipeline.runtime().add_run_observer(observer);

    // The first run records timing and so accumulates worker time; the second has no observer.
    const RunRecord record = record_periods_without_source(
        0, 2, [&pipeline, &handle](std::uint32_t period, SushiProfiler::PeriodTimeline&)
        { run_unobserved_from(pipeline, handle, 1, period); });

    ASSERT_EQ(record.periods.size(), 2u);
    EXPECT_EQ(last_value_of(record, record.periods[1], "worker.busy"), 0.0)
        << "a run that measured nothing writes zero, not what the run before it measured";
}
