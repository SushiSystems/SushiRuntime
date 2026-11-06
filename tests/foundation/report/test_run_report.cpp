/**************************************************************************/
/* test_run_report.cpp                                                    */
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
 * @file test_run_report.cpp
 * @brief Tests what RunReport::reset() keeps and zeroes.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_report.hpp>

using namespace SushiRuntime;

/** @brief Tests what RunReport::reset() keeps and zeroes. */
namespace RunReportTests
{
    /** @brief Tests that reset() keeps the capacity of the report's vectors. */
    TEST(Small_RunReport, ResetKeepsWarningsCapacity)
    {
        RunReport report;
        report.warnings.push_back("first");
        report.warnings.push_back("second");
        report.numa_statistics.push_back(NUMANodeStatistics{});
        const std::size_t warnings_before = report.warnings.capacity();
        const std::size_t numa_before = report.numa_statistics.capacity();

        report.reset();

        EXPECT_TRUE(report.warnings.empty());
        EXPECT_TRUE(report.numa_statistics.empty());
        EXPECT_EQ(report.warnings.capacity(), warnings_before);
        EXPECT_EQ(report.numa_statistics.capacity(), numa_before);
    }

    /** @brief Tests that reset() returns the outcome and the counts to their constructed values. */
    TEST(Small_RunReport, ResetZeroesTheOutcomeAndTheCounts)
    {
        RunReport report;
        report.is_successful = false;
        report.cancelled = true;
        report.total_tasks_executed = 7;
        report.layer = ExecutionLayer::Chain;
        report.distributed_statistics.tasks_offloaded_to_network = 3;

        report.reset();

        EXPECT_TRUE(report.is_successful);
        EXPECT_FALSE(report.cancelled);
        EXPECT_EQ(report.total_tasks_executed, 0u);
        EXPECT_EQ(report.layer, ExecutionLayer::Scheduler);
        EXPECT_EQ(report.distributed_statistics.tasks_offloaded_to_network, 0u);
    }
} // namespace RunReportTests
