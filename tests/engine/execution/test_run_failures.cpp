/**************************************************************************/
/* test_run_failures.cpp                                                  */
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
 * @file test_run_failures.cpp
 * @brief Tests how a run picks the failure it returns when a task and a device queue both failed.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/run_failures.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Execution::merge_run_failures;

namespace
{
    /** @brief Returns a failed result holding @p message. */
    Result<void> queue_failure(const char* message)
    {
        return make_error(Error{ErrorCode::KernelFailed, message});
    }
} // namespace

/** @brief Tests that a run with neither failure succeeds and logs nothing. */
TEST(Small_RunFailures, NeitherFailureSucceedsQuietly)
{
    TestRunner::LogCapture capture;

    const Result<void> merged = merge_run_failures(std::nullopt, Result<void>{});

    EXPECT_TRUE(merged);
    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that a task failure alone is returned as it is and logs nothing. */
TEST(Small_RunFailures, ATaskFailureAloneIsReturned)
{
    TestRunner::LogCapture capture;

    const Result<void> merged =
        merge_run_failures(Error{ErrorCode::InvalidState, "task failed"}, Result<void>{});

    ASSERT_FALSE(merged);
    EXPECT_EQ(merged.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(merged.error().message, "task failed");
    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that the queue's failure alone is returned as it is. */
TEST(Small_RunFailures, AQueueFailureAloneIsReturned)
{
    TestRunner::LogCapture capture;

    const Result<void> merged = merge_run_failures(std::nullopt, queue_failure("queue failed"));

    ASSERT_FALSE(merged);
    EXPECT_EQ(merged.error().message, "queue failed");
    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that with both failures the task failure is returned and the queue's message is logged. */
TEST(Small_RunFailures, LogsTheQueueErrorADroppedTaskErrorHid)
{
    TestRunner::LogCapture capture;

    const Result<void> merged =
        merge_run_failures(Error{ErrorCode::InvalidState, "task failed"}, queue_failure("queue failed"));

    ASSERT_FALSE(merged);
    EXPECT_EQ(merged.error().message, "task failed") << "the task failure wins";

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].module, "execution");
    EXPECT_NE(records[0].message.find("queue failed"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("task failed"), std::string::npos) << records[0].message;
}
