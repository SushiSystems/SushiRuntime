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

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_report.hpp>
#include <SushiRuntime/core/operation_id.hpp>

using namespace SushiRuntime;

/** @brief Tests NodeEventRecord's layout and what RunReport::reset() keeps and zeroes. */
namespace NodeEventRecordTests
{
    /** @brief Tests that NodeEventRecord is 40 bytes and trivially copyable. */
    TEST(Small_NodeEventRecord, LayoutIsFortyBytesAndTriviallyCopyable)
    {
        static_assert(sizeof(NodeEventRecord) == 40u,
                      "NodeEventRecord must stay 40 bytes");
        static_assert(std::is_trivially_copyable<NodeEventRecord>::value,
                      "NodeEventRecord must stay trivially copyable");
        EXPECT_EQ(sizeof(NodeEventRecord), 40u);
        EXPECT_TRUE(std::is_trivially_copyable<NodeEventRecord>::value);
    }

    /** @brief Tests that reset() keeps the capacity of node_events. */
    TEST(Small_NodeEventRecord, ResetKeepsNodeEventsCapacity)
    {
        RunReport report;
        report.node_events.push_back(NodeEventRecord{1, 0, K_WIDTH_UNKNOWN, K_WIDTH_UNKNOWN, 10, 20, 30});
        report.node_events.push_back(NodeEventRecord{2, 0, K_WIDTH_UNKNOWN, K_WIDTH_UNKNOWN, 40, 50, 60});
        const std::size_t capacity_before = report.node_events.capacity();

        report.reset();

        EXPECT_TRUE(report.node_events.empty());
        EXPECT_EQ(report.node_events.capacity(), capacity_before);
    }

    /** @brief Tests that reset() zeroes the clock pairing. */
    TEST(Small_NodeEventRecord, ResetZeroesClockPairing)
    {
        RunReport report;
        report.clock = ClockPairing{100, 200, 5};

        report.reset();

        EXPECT_EQ(report.clock.event_ns, 0u);
        EXPECT_EQ(report.clock.steady_ns, 0u);
        EXPECT_EQ(report.clock.uncertainty_ns, 0u);
    }

    /** @brief Tests that the widths default to K_WIDTH_UNKNOWN. */
    TEST(Small_NodeEventRecord, WidthsDefaultToUnknown)
    {
        const NodeEventRecord rec{};
        EXPECT_EQ(K_WIDTH_UNKNOWN, 0xFFFFFFFFu);
        EXPECT_EQ(rec.launched_width, K_WIDTH_UNKNOWN);
        EXPECT_EQ(rec.live_width, K_WIDTH_UNKNOWN);
    }
} // namespace NodeEventRecordTests

/** @brief Tests that the report types are reachable under their public names. */
namespace RunReportNamesTests
{
    static_assert(std::is_same<decltype(SushiRuntime::NodeTiming::operation),
                               SushiRuntime::OperationID>::value,
                  "NodeTiming::operation must be an OperationID");

    /** @brief Tests that a default report reads as a successful scheduler run with no rows. */
    TEST(Small_RunReportNames, ADefaultReportIsEmpty)
    {
        const SushiRuntime::RunReport report;
        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(report.layer, SushiRuntime::ExecutionLayer::Scheduler);
        EXPECT_TRUE(report.node_events.empty());
        EXPECT_EQ(SushiRuntime::NodeEventRecord().launched_width, SushiRuntime::K_WIDTH_UNKNOWN);
    }

    /** @brief Tests that a NodeTiming names no operation until it is given one. */
    TEST(Small_RunReportNames, ANodeTimingNamesItsOperation)
    {
        using namespace SushiRuntime::Literals;

        SushiRuntime::NodeTiming timing;
        EXPECT_FALSE(timing.operation.is_named());

        timing.operation = "report.names"_op;
        EXPECT_TRUE(timing.operation == "report.names"_op);
    }
} // namespace RunReportNamesTests
