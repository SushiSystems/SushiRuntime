/**************************************************************************/
/* test_profiling_log_category.cpp                                        */
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
 * @file test_profiling_log_category.cpp
 * @brief Tests that the profiling module logs under a category named after its folder.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/span_table.hpp>
#include <SushiProfiler/sites/name_table.hpp>
#include <SushiProfiler/sources/source_context.hpp>
#include <SushiProfiler/sources/span_sink.hpp>

#include <SushiRuntime/profiling/device_source.hpp>
#include <SushiRuntime/profiling/profiling_log_category.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Profiling::DeviceSource;
using SushiRuntime::Internal::Profiling::RunQueue;
using SushiRuntime::Internal::Profiling::RuntimeTracks;

/** @brief Tests that the device source logs its loss report under the module's category in the runtime's project. */
TEST(Small_ProfilingLogCategory, TheDeviceSourceLogsUnderTheModulesCategory)
{
    constexpr std::uint64_t K_PERIOD_END_NS = 10'000'000'000ULL;
    SushiProfiler::NameTable names;
    std::vector<std::string> tracks;
    SushiProfiler::SourceContext context(names, tracks, 0);
    SushiProfiler::SpanTable table;
    SushiProfiler::SpanSink sink(table, names, tracks);
    RunQueue queue(4);
    DeviceSource source(queue, {RuntimeTracks{"node", {"gpu"}}});
    ASSERT_TRUE(source.begin(context).ok());
    RunObservation lossy;
    lossy.drops.per_cause[static_cast<std::size_t>(DropCause::FullRow)] = 1;
    queue.push(0, lossy);
    TestRunner::LogCapture capture;

    source.collect(SushiProfiler::PeriodSpan{0, 0, K_PERIOD_END_NS}, sink);

    const std::vector<TestRunner::CapturedRecord> records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].module, std::string(K_PROFILING_LOG_CATEGORY.module()));
    EXPECT_EQ(records[0].project, std::string(K_PROFILING_LOG_CATEGORY.project().name()));
    source.end();
}
