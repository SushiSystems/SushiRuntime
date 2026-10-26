/**************************************************************************/
/* test_dropped_dispatches.cpp                                            */
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
 * @file test_dropped_dispatches.cpp
 * @brief Tests the one line a run logs for the dispatches it left out of its timing.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/execution/dropped_dispatches.hpp>
#include <SushiRuntime/report/drop_counts.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Execution::log_dropped_dispatches;

namespace
{
    /** @brief Returns counts with @p count lost to @p cause. */
    DropCounts lost(DropCause cause, std::uint32_t count, std::uint32_t first_node = DropCounts::K_NO_NODE)
    {
        DropCounts counts;
        counts.per_cause[static_cast<std::size_t>(cause)] = count;
        counts.first_node = first_node;
        return counts;
    }
} // namespace

/** @brief Tests that a run that lost nothing logs nothing. */
TEST(Small_DroppedDispatches, LogsNothingWhenNoneWasLost)
{
    TestRunner::LogCapture capture;

    log_dropped_dispatches(DropCounts{});

    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that a run that lost dispatches logs one WARN under the execution module. */
TEST(Small_DroppedDispatches, LogsOneLineNamingEachCause)
{
    TestRunner::LogCapture capture;
    DropCounts counts = lost(DropCause::FullRow, 3, 4);
    counts.per_cause[static_cast<std::size_t>(DropCause::UnreadableEvent)] = 2;
    counts.per_cause[static_cast<std::size_t>(DropCause::NoQueue)] = 1;

    log_dropped_dispatches(counts);

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u) << "one line per run, never one per dispatch";
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].module, "execution");
    EXPECT_NE(records[0].message.find("lacks 6 dispatches"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("2 with an unreadable event"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("3 for a full ledger row"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("1 with no profiling queue"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("the first was node 4"), std::string::npos) << records[0].message;
}

/** @brief Tests that a loss that names no node leaves the first node out of the line. */
TEST(Small_DroppedDispatches, ALossThatNamesNoNodeLeavesTheNodeOut)
{
    TestRunner::LogCapture capture;

    log_dropped_dispatches(lost(DropCause::NoQueue, 2));

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].message.find("the first was node"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("2 with no profiling queue"), std::string::npos) << records[0].message;
}
