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
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/sushiruntime.hpp>

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

/** @brief Tests that a session costs one plan rebuild when it starts and one when it ends, and the window costs none. */
TEST(Medium_DeviceTimingWindow, CountsTheRebuildsItCosts)
{
    SessionPipeline pipeline;
    ASSERT_TRUE(pipeline.graph().run().has_value());
    ASSERT_EQ(pipeline.graph().compile_count(), 1u) << "built before any session, without timing";

    std::vector<std::size_t> counts;
    const RunRecord record = record_periods(
        pipeline.runtime(), 1, 2,
        [&pipeline, &counts](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
            counts.push_back(pipeline.graph().compile_count());
        });

    ASSERT_EQ(counts.size(), 3u);
    EXPECT_EQ(counts[0], 2u) << "the session's observer turned timing on: one rebuild";
    EXPECT_EQ(counts[1], 2u) << "a warm-up and an active period are the same plan";
    EXPECT_EQ(counts[2], 2u);
    ASSERT_TRUE(pipeline.graph().run().has_value());
    EXPECT_EQ(pipeline.graph().compile_count(), 3u) << "the session ended and removed the observer: one more rebuild";
    EXPECT_EQ(record.periods.size(), 3u);
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
