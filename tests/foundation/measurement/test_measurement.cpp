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
 * @file test_measurement.cpp
 * @brief Tests summarize(), measure() and the SR_MEASUREMENT line.
 * @see tests/foundation/measurement/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/measurement/measurement.hpp>

using namespace SushiRuntime::Test;

/** @brief Holds the tolerance of every comparison of a summary; the figures are exact. */
constexpr double K_TOLERANCE = 0.0;

/** @brief Tests the median of an odd and an even count, and the extremes. */
TEST(Small_Measurement, SummaryTakesTheMiddleAndTheExtremes)
{
    const RunStatistics odd = summarize({5.0, 1.0, 3.0});
    EXPECT_NEAR(odd.median, 3.0, K_TOLERANCE);
    EXPECT_NEAR(odd.minimum, 1.0, K_TOLERANCE);
    EXPECT_NEAR(odd.maximum, 5.0, K_TOLERANCE);
    EXPECT_EQ(odd.run_count, 3u);

    const RunStatistics even = summarize({4.0, 1.0, 2.0, 3.0});
    EXPECT_NEAR(even.median, 2.5, K_TOLERANCE);
}

/** @brief Tests that no sample gives an empty summary. */
TEST(Small_Measurement, NoSampleGivesAnEmptySummary)
{
    const RunStatistics empty = summarize({});
    EXPECT_EQ(empty.run_count, 0u);
    EXPECT_NEAR(empty.median, 0.0, K_TOLERANCE);
}

/** @brief Tests that measure() calls the run the number of times asked. */
TEST(Small_Measurement, MeasureCallsTheRunEachTime)
{
    std::size_t calls = 0;
    const RunStatistics statistics = measure([&calls] { return static_cast<double>(++calls); }, 3);
    EXPECT_EQ(calls, 3u);
    EXPECT_NEAR(statistics.median, 2.0, K_TOLERANCE);
    EXPECT_EQ(measure([] { return 1.0; }).run_count, K_DEFAULT_RUN_COUNT);
}

/** @brief Tests that the line starts with the prefix and carries every key. */
TEST(Small_Measurement, TheLineCarriesEveryKey)
{
    MeasurementLabel label;
    label.benchmark = "graph";
    label.case_name = "compile";
    label.unit = "ms";
    label.worker_count = 4;
    label.problem_size = 1024;
    const std::string line = format_measurement(label, summarize({1.0, 2.0, 3.0}));

    EXPECT_EQ(line.rfind(K_MEASUREMENT_PREFIX, 0), 0u) << line;
    for (const char* key : {"benchmark=graph", "case=compile", "unit=ms", "median=2", "min=1",
                            "max=3", "runs=3", "workers=4", "size=1024"})
        EXPECT_NE(line.find(key), std::string::npos) << key << " in " << line;
    EXPECT_NE(line.find(std::string("build=") + build_type()), std::string::npos) << line;
}
