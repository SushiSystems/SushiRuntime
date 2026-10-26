/**************************************************************************/
/* test_device_timing_window.cpp                                          */
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
 * @file test_device_timing_window.cpp
 * @brief Tests that device timing follows the observers a session adds, and the capture window decides what it keeps.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

/** @brief Tests that with a window of one warm-up and two active periods the record holds spans for exactly those three. */
TEST(Medium_DeviceTimingWindow, SpansExistOnlyForTheRecordedPeriods)
{
    SessionPipeline pipeline;

    // Six periods are marked and run; the window keeps the first three.
    const RunRecord record = record_periods(
        pipeline.runtime(), 1, 2,
        [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline& timeline)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
            // Three runs follow the window's last period; their periods are not recorded.
            if (timeline.current() == 2)
                for (int extra = 0; extra < 3; ++extra)
                {
                    timeline.mark();
                    EXPECT_TRUE(pipeline.graph().run().has_value());
                }
        });

    ASSERT_EQ(record.periods.size(), 3u);
    EXPECT_TRUE(record.periods[0].warmup);
    EXPECT_FALSE(record.periods[1].warmup);
    std::size_t spans = 0;
    for (const PeriodRecord& period : record.periods)
        spans += period.spans.size();
    EXPECT_EQ(spans, 9u) << "three nodes in each of the three recorded periods, none from the periods after the window";
}

/** @brief Tests that a session costs no plan rebuild, when it starts, in its window or when it ends. */
TEST(Medium_DeviceTimingWindow, CostsNoRebuild)
{
    SessionPipeline pipeline;
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u) << "built before any session, with no observer";

    std::vector<std::size_t> counts;
    const RunRecord record = record_periods(
        pipeline.runtime(), 1, 2,
        [&pipeline, &counts](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
            counts.push_back(pipeline.graph().compile_count());
        });

    ASSERT_EQ(counts.size(), 3u);
    for (const std::size_t count : counts)
        EXPECT_EQ(count, 1u) << "the session's observer turned timing on for each run, with no rebuild";
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "the session ended and removed the observer, with no rebuild";
    ASSERT_EQ(record.periods.size(), 3u);
    for (const PeriodRecord& period : record.periods)
        EXPECT_EQ(period.spans.size(), 3u) << "the run after the add was timed, though its plan was built before";
}

/** @brief Tests that a runtime with no session records no device spans in a record made without its source. */
TEST(Medium_DeviceTimingWindow, ARecordMadeWithoutTheSourceHoldsNoDeviceSpans)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 2, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&) { EXPECT_TRUE(pipeline.graph().run().has_value()); });

    for (const PeriodRecord& period : record.periods)
        EXPECT_EQ(period.spans.size(), 0u);
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "no observer, so no rebuild for timing";
}

/** @brief Tests that pausing the session stops the parts' device timing, leaves an ungated observer timed, and resuming starts it again. */
TEST(Medium_DeviceTimingWindow, APausedSessionCostsNoDeviceTiming)
{
    SessionPipeline pipeline;
    SushiProfiler::Registry registry;
    SushiRuntime::RuntimeProfilerParts parts = SushiRuntime::register_profiler_parts(registry, pipeline.runtime());
    const auto ungated = std::make_shared<TestRunner::RecordingObserver>();
    const RunObserverHandle ungated_handle = pipeline.runtime().add_run_observer(ungated);

    SushiProfiler::SessionStart start = SushiProfiler::Session::start(session_config(0, 3), registry);
    ASSERT_TRUE(start.ok()) << start.error;
    SushiProfiler::PeriodTimeline& timeline = start.session->timeline("run.step");

    timeline.mark();
    ASSERT_TRUE(start.session->pause());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    timeline.mark();
    ASSERT_TRUE(start.session->resume());
    ASSERT_TRUE(pipeline.graph().run().has_value());
    timeline.mark();
    ASSERT_TRUE(pipeline.graph().run().has_value());
    timeline.mark();
    const RunRecord record = std::move(start.session->stop().record);

    ASSERT_EQ(ungated->count(), 3u);
    for (const RunObservation& observation : ungated->runs())
        EXPECT_EQ(observation.events.size(), 3u) << "the ungated observer is timed, paused or not";
    EXPECT_EQ(pipeline.graph().compile_count(), 1u) << "pausing and resuming rebuild nothing";
    ASSERT_EQ(record.periods.size(), 3u);
    EXPECT_EQ(record.periods[0].spans.size(), 0u) << "no device span reaches a paused session";
    EXPECT_EQ(record.periods[1].spans.size(), 3u) << "the run after the resume is timed for the session";
    EXPECT_EQ(record.periods[2].spans.size(), 3u);
}
