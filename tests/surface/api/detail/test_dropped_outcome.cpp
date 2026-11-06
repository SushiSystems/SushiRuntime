/**************************************************************************/
/* test_dropped_outcome.cpp                                               */
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
 * @file test_dropped_outcome.cpp
 * @brief Tests log_dropped_outcome: the WARN a step nobody waited for leaves when it failed.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/api/detail/dropped_outcome.hpp>

#include <gtest/gtest.h>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;

/** @brief Tests that the line names the error, its message and the call that dropped it. */
TEST(Small_DroppedOutcome, LogsTheErrorAndWhereItWasDropped)
{
    TestRunner::LogCapture capture;
    Detail::log_dropped_outcome(Error{ErrorCode::KernelFailed, "kernel exploded"},
                                "the destruction of a RunHandle");

    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 1U);
    EXPECT_EQ(capture.count_in("api"), 1U);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "kernel exploded"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "the destruction of a RunHandle"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "its outcome is dropped"));
}
