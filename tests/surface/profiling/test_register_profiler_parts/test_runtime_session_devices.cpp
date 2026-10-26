/**************************************************************************/
/* test_runtime_session_devices.cpp                                       */
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
 * @file test_runtime_session_devices.cpp
 * @brief Tests a session over a GPU and over two devices, the cases that need hardware the CPU device cannot stand in for.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

namespace
{
    /** @brief The element count of the GPU run: large enough that a kernel outlasts the submit call. */
    constexpr std::size_t K_GPU_ELEMENTS = std::size_t{1} << 24;

    /** @brief Returns the index of the first device of @p runtime of class @p kind, or the count when none is. */
    std::size_t first_device_of(Runtime& runtime, DeviceKind kind)
    {
        const auto& devices = runtime.advanced().devices();
        for (std::size_t index = 0; index < devices.size(); ++index)
            if (devices[index].kind == kind)
                return index;
        return devices.size();
    }
} // namespace

/** @brief Tests that the longest span of a period on the GPU lasts longer than the uncertainty of its device's pairing. */
TEST(Large_RuntimeSession, SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration)
{
    auto probe = Runtime::create().value();
    const std::size_t gpu = first_device_of(probe, DeviceKind::GPU);
    if (gpu == probe.advanced().device_count())
        GTEST_SKIP() << "no GPU device is available";

    SessionPipeline pipeline(SessionPipeline::scheduler_config(),
                             DeviceIndex{static_cast<std::uint32_t>(gpu)}, K_GPU_ELEMENTS);
    const RunRecord record = record_periods(pipeline.runtime(), 1, 2,
                                            [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                            {
                                                EXPECT_TRUE(pipeline.graph().run().has_value());
                                            });

    ASSERT_EQ(record.periods.size(), 3u);
    for (const PeriodRecord& period : record.periods)
    {
        ASSERT_EQ(period.spans.size(), 3u);
        std::uint64_t longest = 0;
        std::uint32_t uncertainty = 0;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            if (span.end_ns - span.start_ns >= longest)
            {
                longest = span.end_ns - span.start_ns;
                uncertainty = span.uncertainty_ns;
            }
        }
        EXPECT_GT(uncertainty, 0u) << "a device span carries the uncertainty of its pairing";
        EXPECT_LT(uncertainty, longest) << "the longest kernel of period " << period.index << " outlasts the submit call";
    }
}

/** @brief Tests that with two devices each gets its own track and its spans are all placed. */
TEST(Large_RuntimeSession, TwoDevicesGetOneTrackAndOnePairingEach)
{
    auto runtime = Runtime::create().value();
    if (runtime.advanced().device_count() < 2)
        GTEST_SKIP() << "a second device is not available";

    constexpr std::size_t K_ELEMENTS = 1024;
    auto first = runtime.buffer<float>(K_ELEMENTS, DeviceIndex{0}).value();
    auto second = runtime.buffer<float>(K_ELEMENTS, DeviceIndex{1}).value();
    float* a = first.data();
    float* b = second.data();
    auto graph = runtime.graph();
    NodeOptions on_first;
    on_first.name = "on_device_0";
    NodeOptions on_second;
    on_second.name = "on_device_1";
    graph.add(K_ELEMENTS, Reads{}, Writes(first), [a](std::size_t i) { a[i] = static_cast<float>(i); }, on_first);
    graph.add(K_ELEMENTS, Reads{}, Writes(second), [b](std::size_t i) { b[i] = static_cast<float>(i); }, on_second);

    const RunRecord record = record_periods(runtime, 1, 2,
                                            [&graph](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                            {
                                                EXPECT_TRUE(graph.run().has_value());
                                            });

    ASSERT_EQ(record.periods.size(), 3u);
    for (const PeriodRecord& period : record.periods)
    {
        ASSERT_EQ(period.spans.size(), 2u);
        std::set<std::string> tracks;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
            tracks.insert(record.tracks.at(period.spans.span(row).track));
        EXPECT_EQ(tracks, (std::set<std::string>{device_track_name(runtime, 0), device_track_name(runtime, 1)}));
    }
    EXPECT_EQ(record.drops.spans_dropped, 0u) << "every span found its own device's pairing";
}
