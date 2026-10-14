/**************************************************************************/
/* test_logger.cpp                                                        */
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
 * @file test_logger.cpp
 * @brief Tests the log level gate, the sink and both delivery modes.
 * @see tests/foundation/logging/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Holds what a test sink saw of one record. */
    struct SeenRecord
    {
        LogLevel level;   /**< The level the sink was called with. */
        std::string line;       /**< A copy of the line. */
        std::thread::id thread; /**< The thread the sink ran on. */
    };

    /** @brief Collects the records a test sink receives. */
    using SeenRecords = std::vector<SeenRecord>;

    /** @brief Returns a sink that appends each record to @p records; see README.md. */
    LogSink collect_into(SeenRecords* records)
    {
        return [records](LogLevel level, std::string_view line)
        {
            records->push_back(SeenRecord{level, std::string(line), std::this_thread::get_id()});
        };
    }

    /** @brief Puts the logger in synchronous mode and restores the level and the sink. */
    class Unit_LoggerTest : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                saved_level_ = log_level();
                set_log_synchronous(true);
                set_log_level(LogLevel::Warn);
            }

            void TearDown() override
            {
                set_log_synchronous(true);
                set_log_sink(nullptr);
                set_log_level(saved_level_);
            }

        private:
            /// @brief The level the process had when the test began.
            LogLevel saved_level_ = LogLevel::Warn;
    };

}

/** @brief Tests that a call above the process level leaves its arguments unevaluated. */
TEST_F(Unit_LoggerTest, DisabledLevelDoesNotEvaluateItsArguments)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));
    int evaluations = 0;

    set_log_level(LogLevel::Error);
    SR_LOG_WARN("evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 0);
    EXPECT_TRUE(records.empty());

    set_log_level(LogLevel::Warn);
    SR_LOG_WARN("evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that set_log_level decides which calls reach the sink. */
TEST_F(Unit_LoggerTest, SetLogLevelTakesEffect)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_level(LogLevel::Warn);
    EXPECT_EQ(log_level(), LogLevel::Warn);
    SR_LOG_INFO("below the level");
    EXPECT_TRUE(records.empty());

    set_log_level(LogLevel::Info);
    EXPECT_EQ(log_level(), LogLevel::Info);
    SR_LOG_INFO("at the level");
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Info);

    set_log_level(LogLevel::Off);
    SR_LOG_ERROR("nothing passes Off");
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that the sink receives the level and the formatted line, on the caller. */
TEST_F(Unit_LoggerTest, SinkReceivesLevelAndText)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    SR_LOG_WARN("value {} of {}", 3, "seven");

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Warn);
    EXPECT_NE(records[0].line.find("value 3 of seven"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("[WARN]"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("test_logger.cpp"), std::string::npos) << records[0].line;
    EXPECT_EQ(records[0].line.find('\n'), std::string::npos);
    EXPECT_EQ(records[0].thread, std::this_thread::get_id());
}

/** @brief Tests that log_write hands its line to the sink unchanged and unfiltered. */
TEST_F(Unit_LoggerTest, LogWriteBypassesTheLevel)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));
    set_log_level(LogLevel::Off);

    log_write(LogLevel::Debug, std::string("a finished line"));

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Debug);
    EXPECT_EQ(records[0].line, "a finished line");
}

/** @brief Tests that an empty sink sends records to stderr and retires the old sink. */
TEST_F(Unit_LoggerTest, EmptySinkRestoresTheDefault)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));
    SR_LOG_ERROR("to the test sink");
    ASSERT_EQ(records.size(), 1u);

    set_log_sink(nullptr);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    SR_LOG_ERROR("restored {}", 1);
    const std::string standard_error = testing::internal::GetCapturedStderr();
    const std::string standard_output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(records.size(), 1u);
    EXPECT_NE(standard_error.find("restored 1\n"), std::string::npos) << standard_error;
    EXPECT_TRUE(standard_output.empty()) << standard_output;
}

/** @brief Tests that a record whose sink throws is written to stderr and the call returns. */
TEST_F(Unit_LoggerTest, ThrowingSinkLeavesTheRecordOnStderr)
{
    set_log_sink([](LogLevel, std::string_view)
    {
        throw std::runtime_error("sink failure");
    });

    testing::internal::CaptureStderr();
    EXPECT_NO_THROW({ SR_LOG_ERROR("kept {}", 2); });
    const std::string standard_error = testing::internal::GetCapturedStderr();

    EXPECT_NE(standard_error.find("sink failure"), std::string::npos) << standard_error;
    EXPECT_NE(standard_error.find("kept 2\n"), std::string::npos) << standard_error;
}

/** @brief Tests that asynchronous records run the sink on another thread, in order. */
TEST_F(Unit_LoggerTest, AsynchronousRecordsArriveBeforeSynchronousModeReturns)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_synchronous(false);
    SR_LOG_WARN("record {}", 0);
    SR_LOG_WARN("record {}", 1);
    SR_LOG_WARN("record {}", 2);
    set_log_synchronous(true);

    ASSERT_EQ(records.size(), 3u);
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const std::string expected = "record " + std::to_string(i);
        EXPECT_NE(records[i].line.find(expected), std::string::npos) << records[i].line;
        EXPECT_NE(records[i].thread, std::this_thread::get_id());
    }
}
