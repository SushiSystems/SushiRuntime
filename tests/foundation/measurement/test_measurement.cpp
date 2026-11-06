/**************************************************************************/
/* test_measurement.cpp                                                   */
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
 * @file test_measurement.cpp
 * @brief Tests summarize(), measure() and the SR_MEASUREMENT line.
 * @see tests/foundation/measurement/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/measurement/measurement.hpp>

using namespace SushiRuntime::Test;

/** @brief Tests the median of an odd count. */
TEST(Small_Measurement, TheMedianOfAnOddCountIsTheMiddleSample)
{
    EXPECT_EQ(summarize({5.0, 1.0, 3.0}).median, 3.0);
}

/** @brief Tests the median of an even count. */
TEST(Small_Measurement, TheMedianOfAnEvenCountIsTheMeanOfTheMiddlePair)
{
    EXPECT_EQ(summarize({4.0, 1.0, 2.0, 3.0}).median, 2.5);
}

/** @brief Tests the extremes and the run count of a summary. */
TEST(Small_Measurement, SummaryTakesTheExtremesAndCountsTheRuns)
{
    const RunStatistics statistics = summarize({5.0, 1.0, 3.0});
    EXPECT_EQ(statistics.minimum, 1.0);
    EXPECT_EQ(statistics.maximum, 5.0);
    EXPECT_EQ(statistics.run_count, 3u);
}

/** @brief Tests that no sample gives an all-zero summary. */
TEST(Small_Measurement, NoSampleGivesAnEmptySummary)
{
    const RunStatistics empty = summarize({});
    EXPECT_EQ(empty.run_count, 0u);
    EXPECT_EQ(empty.median, 0.0);
    EXPECT_EQ(empty.minimum, 0.0);
    EXPECT_EQ(empty.maximum, 0.0);
}

/** @brief Tests that measure() calls the run the number of times asked. */
TEST(Small_Measurement, MeasureCallsTheRunEachTime)
{
    std::size_t calls = 0;
    static_cast<void>(measure([&calls] { return static_cast<double>(++calls); }, 3));
    EXPECT_EQ(calls, 3u);
}

/** @brief Tests that the line opens with the prefix and carries every key with its value. */
TEST(Small_Measurement, TheLineCarriesEveryKey)
{
    MeasurementLabel label;
    label.benchmark = "graph";
    label.case_name = "compile";
    label.unit = "ms";
    label.worker_count = 4;
    label.problem_size = 1024;
    const std::string line = format_measurement(label, summarize({1.0, 2.0, 3.0}));

    EXPECT_EQ(line.rfind("SR_MEASUREMENT benchmark=graph case=compile unit=ms median=2 min=1 max=3 runs=3 build=", 0), 0u)
        << line;
    EXPECT_NE(line.find(" workers=4 size=1024"), std::string::npos) << line;
}

/** @brief Tests that the line names the build as release or debug. */
TEST(Small_Measurement, TheLineNamesTheBuild)
{
    const std::string line = format_measurement(MeasurementLabel{}, summarize({1.0}));
    const bool names_a_build = line.find(" build=release ") != std::string::npos ||
                               line.find(" build=debug ") != std::string::npos;

    EXPECT_TRUE(names_a_build) << line;
}
