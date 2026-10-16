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
 * @brief Tests the SR_LOG macros: their gates, the record they make and FATAL.
 * @see tests/foundation/logging/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_project.hpp>
#include <SushiLog/log/log_record.hpp>
#include <SushiLog/log/log_sink.hpp>

#include <SushiRuntime/log/logger.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the project every test category belongs to. */
    constexpr SushiLog::LogProject K_LOGGER_TEST_LOG_PROJECT{"logger_test", "SR_LOGGER_TEST_LOG_LEVEL"};

    /** @brief Names the category most tests log under. */
    constexpr SushiLog::LogCategory K_ALPHA_LOG_CATEGORY =
        SushiLog::make_log_category(K_LOGGER_TEST_LOG_PROJECT, "alpha");

    /** @brief Holds what a test sink saw of one record. */
    struct SeenRecord
    {
        SushiLog::LogLevel level;            /**< The level of the record. */
        std::string project;       /**< A copy of its project. */
        std::string module;        /**< A copy of its module. */
        std::string file;          /**< A copy of its file. */
        std::uint32_t line_number; /**< Its line. */
        std::uint32_t number;      /**< The logger's number for the thread that logged it. */
        std::int64_t time_ns;      /**< Its time. */
        std::string message;       /**< A copy of its message. */
        std::string line;          /**< A copy of its whole line. */
        std::thread::id thread;    /**< The thread the sink ran on. */
    };

    /** @brief Returns what a sink sees of @p record, on the calling thread. */
    SeenRecord seen(const SushiLog::LogRecord& record)
    {
        return SeenRecord{record.level,
                          std::string(record.project),
                          std::string(record.module),
                          std::string(record.file),
                          record.line,
                          record.thread,
                          record.time_ns,
                          std::string(record.message),
                          std::string(record.text),
                          std::this_thread::get_id()};
    }

    /** @brief Collects the records a test sink receives. */
    using SeenRecords = std::vector<SeenRecord>;

    /** @brief Returns a sink that appends each record to @p records; see README.md. */
    SushiLog::LogSink collect_into(SeenRecords* records)
    {
        return [records](const SushiLog::LogRecord& record)
        {
            records->push_back(seen(record));
        };
    }

    /** @brief Puts the logger in synchronous mode and restores the levels and the sink. */
    class Medium_LoggerTest : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                // SushiLog sets a project only once it has seen one of its categories.
                (void)SushiLog::log_level(K_ALPHA_LOG_CATEGORY);
                saved_level_ = SushiLog::log_level(K_LOGGER_TEST_LOG_PROJECT);
                SushiLog::set_log_synchronous(true);
                SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Warn);
            }

            void TearDown() override
            {
                SushiLog::set_log_synchronous(true);
                SushiLog::set_log_sink(nullptr);
                SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, saved_level_);
            }

        private:
            /// @brief The level the test project had when the test began.
            SushiLog::LogLevel saved_level_ = SushiLog::LogLevel::Warn;
    };

}

/** @brief Tests that a call below its category's level leaves its arguments unevaluated. */
TEST_F(Medium_LoggerTest, DisabledLevelDoesNotEvaluateItsArguments)
{
    SeenRecords records;
    SushiLog::set_log_sink(collect_into(&records));
    int evaluations = 0;

    SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Error);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 0);
    EXPECT_TRUE(records.empty());

    SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Warn);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that SushiLog::set_log_level decides which calls reach the sink. */
TEST_F(Medium_LoggerTest, SetLogLevelTakesEffect)
{
    SeenRecords records;
    SushiLog::set_log_sink(collect_into(&records));

    SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Warn);
    EXPECT_EQ(SushiLog::log_level(K_LOGGER_TEST_LOG_PROJECT), SushiLog::LogLevel::Warn);
    SR_LOG_INFO(K_ALPHA_LOG_CATEGORY, "below the level");
    EXPECT_TRUE(records.empty());

    SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Info);
    EXPECT_EQ(SushiLog::log_level(K_LOGGER_TEST_LOG_PROJECT), SushiLog::LogLevel::Info);
    SR_LOG_INFO(K_ALPHA_LOG_CATEGORY, "at the level");
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Info);

    SushiLog::set_log_level(K_LOGGER_TEST_LOG_PROJECT, SushiLog::LogLevel::Off);
    SR_LOG_ERROR(K_ALPHA_LOG_CATEGORY, "nothing passes Off");
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that the record carries every field apart and the line joins them. */
TEST_F(Medium_LoggerTest, SinkReceivesEveryFieldAndTheLine)
{
    SeenRecords records;
    SushiLog::set_log_sink(collect_into(&records));

    const std::int64_t before = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "value {} of {}", 3, "seven");

    ASSERT_EQ(records.size(), 1u);
    const SeenRecord& record = records[0];
    EXPECT_EQ(record.level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(record.project, "logger_test");
    EXPECT_EQ(record.module, "alpha");
    EXPECT_EQ(record.file, "test_logger.cpp");
    EXPECT_GT(record.line_number, 0u);
    EXPECT_GT(record.number, 0u);
    EXPECT_GE(record.time_ns, before);
    EXPECT_EQ(record.message, "value 3 of seven");
    EXPECT_NE(record.line.find("[WARN] [logger_test.alpha] [t" + std::to_string(record.number) +
                               "] [test_logger.cpp:" + std::to_string(record.line_number) +
                               "] value 3 of seven"),
              std::string::npos)
        << record.line;
    EXPECT_EQ(record.line.find('\n'), std::string::npos);
    EXPECT_EQ(record.thread, std::this_thread::get_id());
}

/** @brief Tests that a FATAL record reaches the sink after every queued one, on the caller. */
TEST_F(Medium_LoggerTest, FatalIsDeliveredAfterTheQueueOnTheCallingThread)
{
    SeenRecords records;
    SushiLog::set_log_sink(collect_into(&records));

    SushiLog::set_log_synchronous(false);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "queued {}", 0);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "queued {}", 1);
    SR_LOG_FATAL(K_ALPHA_LOG_CATEGORY, "cannot continue");

    ASSERT_EQ(records.size(), 3u);
    EXPECT_NE(records[0].line.find("queued 0"), std::string::npos) << records[0].line;
    EXPECT_NE(records[1].line.find("queued 1"), std::string::npos) << records[1].line;
    EXPECT_EQ(records[2].level, SushiLog::LogLevel::Fatal);
    EXPECT_NE(records[2].line.find("[FATAL]"), std::string::npos) << records[2].line;
    EXPECT_EQ(records[2].thread, std::this_thread::get_id());
}

