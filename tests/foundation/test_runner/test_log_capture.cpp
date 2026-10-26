/**************************************************************************/
/* test_log_capture.cpp                                                   */
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

/**
 * @file test_log_capture.cpp
 * @brief Tests LogCapture, the shared capture sink of the runtime's tests.
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the category the tests log under, a module of the runtime's project. */
    constexpr SushiLog::LogCategory K_CAPTURE_TEST_LOG_CATEGORY =
        SushiLog::make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "capture_test");
}

/** @brief Tests that the capture keeps the records the level passes and nothing below it. */
TEST(Small_LogCapture, RecordsOnlyWhatTheLevelPasses)
{
    TestRunner::LogCapture capture;

    SR_LOG_DEBUG(K_CAPTURE_TEST_LOG_CATEGORY, "debug {}", 1);
    SR_LOG_WARN(K_CAPTURE_TEST_LOG_CATEGORY, "warn {}", 2);

    const std::vector<TestRunner::CapturedRecord> records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].project, "sushiruntime");
    EXPECT_EQ(records[0].module, "capture_test");
    EXPECT_EQ(records[0].message, "warn 2");
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Debug), 0u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "warn"));
}

/** @brief Tests that the destructor puts the level back and the default sink in place. */
TEST(Small_LogCapture, RestoresTheSinkAndTheLevelOnDestruction)
{
    const SushiLog::LogLevel before = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
    {
        TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Trace);
        EXPECT_EQ(SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT), SushiLog::LogLevel::Trace);
    }
    EXPECT_EQ(SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT), before);

    // A second capture starts empty, so the first one's sink is gone.
    TestRunner::LogCapture second;
    EXPECT_TRUE(second.records().empty());
}
