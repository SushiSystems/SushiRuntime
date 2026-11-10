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
 * @file test_runtime_session_devices.cpp
 * @brief Tests a session over a GPU and over two devices, the cases that need hardware the CPU device cannot stand in for.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "support/runtime_environment.hpp"

#include "profiled_pipelines.hpp"

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

    /** @brief The duration and the pairing uncertainty of the longest span of a period. */
    struct LongestSpan
    {
        std::uint64_t duration_ns = 0;   /**< How long the span lasted. */
        std::uint32_t uncertainty_ns = 0; /**< The uncertainty of its device's pairing. */
    };

    /** @brief Returns the longest span of @p period; the last of equal ones wins. */
    LongestSpan longest_span_of(const PeriodRecord& period)
    {
        LongestSpan longest;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            const std::uint64_t duration = span.end_ns - span.start_ns;
            if (duration >= longest.duration_ns)
                longest = LongestSpan{duration, span.uncertainty_ns};
        }
        return longest;
    }

    /** @brief Returns the tracks the spans of @p period lie on. */
    std::set<std::string> tracks_of(const RunRecord& record, const PeriodRecord& period)
    {
        std::set<std::string> tracks;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
            tracks.insert(record.tracks.at(period.spans.span(row).track));
        return tracks;
    }

    /**
     * @brief Describes the bounds of @p period, each of its spans and the spans the record lost.
     *
     * Printed when a period holds fewer spans than the run made, so a failure shows which span left, how
     * far outside the period it started and what pairing uncertainty it carried.
     */
    std::string describe_period(const RunRecord& record, const PeriodRecord& period)
    {
        std::ostringstream text;
        text << "period " << period.index << " [" << period.begin_ns << ", " << period.end_ns << ") ns, warmup "
             << period.warmup << ", record dropped " << record.drops.spans_dropped << " spans";
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            text << "\n  span " << row << " track " << record.tracks.at(span.track) << " start " << span.start_ns
                 << " end " << span.end_ns << " uncertainty " << span.uncertainty_ns;
        }
        return text.str();
    }

    /** @brief Expects the longest span of @p period to carry a pairing uncertainty and, outside warm-up, to outlast it. */
    void expect_the_uncertainty_below_the_longest_span(const RunRecord& record, const PeriodRecord& period)
    {
        SCOPED_TRACE(describe_period(record, period));
        ASSERT_EQ(period.spans.size(), 3u);
        const LongestSpan longest = longest_span_of(period);
        EXPECT_GT(longest.uncertainty_ns, 0u) << "a device span carries the uncertainty of its pairing";
        // The warm-up run's first submit includes the driver's compilation (SPEC, current state 7).
        if (!period.warmup)
            EXPECT_LT(longest.uncertainty_ns, longest.duration_ns) << "the longest kernel outlasts the submit call";
    }

    /** @brief Skips a test that needs a GPU when the program's runtime has none. */
    class Large_RuntimeSessionOnAGpu : public TestSupport::RuntimeTest
    {
        protected:
            void SetUp() override
            {
                if (first_device_of(runtime(), DeviceKind::GPU) == runtime().advanced().device_count())
                    GTEST_SKIP() << "no GPU device is available";
            }
    };

    /** @brief Skips a test that needs two devices when the program's runtime has fewer. */
    class Large_RuntimeSessionOnTwoDevices : public TestSupport::RuntimeTest
    {
        protected:
            void SetUp() override
            {
                if (runtime().advanced().device_count() < 2)
                    GTEST_SKIP() << "a second device is not available";
            }
    };
} // namespace

/** @brief Tests that the longest span of a period on the GPU lasts longer than the uncertainty of its device's pairing. */
TEST_F(Large_RuntimeSessionOnAGpu, SpansOnTheGpuHaveAPairingUncertaintyBelowTheirDuration)
{
    const std::size_t gpu = first_device_of(runtime(), DeviceKind::GPU);
    SessionPipeline pipeline(SessionPipeline::scheduler_config(),
                             DeviceIndex{static_cast<std::uint32_t>(gpu)}, K_GPU_ELEMENTS);

    const RunRecord record = record_periods(pipeline.runtime(), 1, 2,
                                            [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                            {
                                                EXPECT_TRUE(pipeline.graph().run().has_value());
                                            });

    ASSERT_EQ(record.periods.size(), 3u);
    expect_the_uncertainty_below_the_longest_span(record, record.periods[0]);
    expect_the_uncertainty_below_the_longest_span(record, record.periods[1]);
    expect_the_uncertainty_below_the_longest_span(record, record.periods[2]);
    EXPECT_FALSE(record.periods.back().warmup) << "the active periods are the ones compared";
}

/** @brief Tests that with two devices each gets its own track and its spans are all placed. */
TEST_F(Large_RuntimeSessionOnTwoDevices, TwoDevicesGetOneTrackAndOnePairingEach)
{
    constexpr std::size_t K_ELEMENTS = 1024;
    auto first = runtime().buffer<float>(K_ELEMENTS, DeviceIndex{0}).value();
    auto second = runtime().buffer<float>(K_ELEMENTS, DeviceIndex{1}).value();
    float* a = first.data();
    float* b = second.data();
    auto graph = runtime().graph();
    NodeOptions on_first;
    on_first.name = "on_device_0";
    NodeOptions on_second;
    on_second.name = "on_device_1";
    graph.add(K_ELEMENTS, Reads{}, Writes(first), [a](std::size_t i) { a[i] = static_cast<float>(i); }, on_first);
    graph.add(K_ELEMENTS, Reads{}, Writes(second), [b](std::size_t i) { b[i] = static_cast<float>(i); }, on_second);

    const RunRecord record = record_periods(runtime(), 1, 2,
                                            [&graph](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                            {
                                                EXPECT_TRUE(graph.run().has_value());
                                            });

    ASSERT_EQ(record.periods.size(), 3u);
    const std::set<std::string> expected{device_track_name(runtime(), 0), device_track_name(runtime(), 1)};
    ASSERT_EQ(record.periods[0].spans.size(), 2u);
    EXPECT_EQ(tracks_of(record, record.periods[0]), expected);
    ASSERT_EQ(record.periods[1].spans.size(), 2u);
    EXPECT_EQ(tracks_of(record, record.periods[1]), expected);
    ASSERT_EQ(record.periods[2].spans.size(), 2u);
    EXPECT_EQ(tracks_of(record, record.periods[2]), expected);
    EXPECT_EQ(record.drops.spans_dropped, 0u) << "every span found its own device's pairing";
}
