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
 * @file test_run_report.cpp
 * @brief Tests the node event records and the names a RunReport gives its values.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/report/run_report.hpp>

using namespace SushiRuntime;

/** @brief Tests NodeEventRecord's layout and what RunReport::reset() keeps and zeroes. */
namespace NodeEventRecordTests
{
    /** @brief Tests that NodeEventRecord is 48 bytes and trivially copyable. */
    TEST(Small_NodeEventRecord, LayoutIsFortyEightBytesAndTriviallyCopyable)
    {
        static_assert(sizeof(NodeEventRecord) == 48u,
                      "NodeEventRecord must stay 48 bytes");
        static_assert(std::is_trivially_copyable<NodeEventRecord>::value,
                      "NodeEventRecord must stay trivially copyable");
        static_assert(offsetof(NodeEventRecord, end_ns) == 32u &&
                          offsetof(NodeEventRecord, device_index) == 40u &&
                          offsetof(NodeEventRecord, submit_thread) == 44u,
                      "the two appended fields follow end_ns");
        EXPECT_EQ(sizeof(NodeEventRecord), 48u);
        EXPECT_TRUE(std::is_trivially_copyable<NodeEventRecord>::value);
    }

    /** @brief Tests that reset() keeps the capacity of the report's vectors. */
    TEST(Small_NodeEventRecord, ResetKeepsWarningsCapacity)
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
    TEST(Small_NodeEventRecord, ResetZeroesTheOutcomeAndTheCounts)
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

    /** @brief Tests that the widths default to K_WIDTH_UNKNOWN and the device to the host. */
    TEST(Small_NodeEventRecord, WidthsDefaultToUnknown)
    {
        const NodeEventRecord rec{};
        EXPECT_EQ(K_WIDTH_UNKNOWN, 0xFFFFFFFFu);
        EXPECT_EQ(rec.launched_width, K_WIDTH_UNKNOWN);
        EXPECT_EQ(rec.live_width, K_WIDTH_UNKNOWN);
        EXPECT_EQ(rec.device_index, K_HOST_DEVICE_INDEX);
        EXPECT_EQ(rec.submit_thread, 0u);
    }
} // namespace NodeEventRecordTests

/** @brief Tests that the report types are reachable under their public names. */
namespace RunReportNamesTests
{
    /** @brief Tests that a default report reads as a successful scheduler run with no rows. */
    TEST(Small_RunReportNames, ADefaultReportIsEmpty)
    {
        const SushiRuntime::RunReport report;
        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(report.layer, SushiRuntime::ExecutionLayer::Scheduler);
        EXPECT_TRUE(report.numa_statistics.empty());
        EXPECT_EQ(SushiRuntime::NodeEventRecord().launched_width, SushiRuntime::K_WIDTH_UNKNOWN);
    }
} // namespace RunReportNamesTests
