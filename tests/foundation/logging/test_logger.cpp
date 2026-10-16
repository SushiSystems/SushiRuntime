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
 * @brief Tests the levels of each category, the record, the sink, the queue and FATAL.
 * @see tests/foundation/logging/README.md
 * @author Mustafa Garip
 */

#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <mutex>
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
    /** @brief Names the category most tests log under. */
    constexpr LogCategory K_ALPHA_LOG_CATEGORY = make_log_category("logger_test_alpha");

    /** @brief Names a second category, whose level the tests set apart. */
    constexpr LogCategory K_BETA_LOG_CATEGORY = make_log_category("logger_test_beta");

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
    class Medium_LoggerTest : public ::testing::Test
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

/** @brief Tests that a call below its category's level leaves its arguments unevaluated. */
TEST_F(Medium_LoggerTest, DisabledLevelDoesNotEvaluateItsArguments)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));
    int evaluations = 0;

    set_log_level(LogLevel::Error);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 0);
    EXPECT_TRUE(records.empty());

    set_log_level(LogLevel::Warn);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "evaluated {} times", ++evaluations);
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that set_log_level decides which calls reach the sink. */
TEST_F(Medium_LoggerTest, SetLogLevelTakesEffect)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_level(LogLevel::Warn);
    EXPECT_EQ(log_level(), LogLevel::Warn);
    SR_LOG_INFO(K_ALPHA_LOG_CATEGORY, "below the level");
    EXPECT_TRUE(records.empty());

    set_log_level(LogLevel::Info);
    EXPECT_EQ(log_level(), LogLevel::Info);
    SR_LOG_INFO(K_ALPHA_LOG_CATEGORY, "at the level");
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Info);

    set_log_level(LogLevel::Off);
    SR_LOG_ERROR(K_ALPHA_LOG_CATEGORY, "nothing passes Off");
    EXPECT_EQ(records.size(), 1u);
}

/** @brief Tests that the record carries the level, the category, the thread and the call site. */
TEST_F(Medium_LoggerTest, SinkReceivesLevelAndText)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "value {} of {}", 3, "seven");

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Warn);
    EXPECT_NE(records[0].line.find("value 3 of seven"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("[WARN]"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("[logger_test_alpha]"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("] [t"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("[test_logger.cpp:"), std::string::npos) << records[0].line;
    EXPECT_EQ(records[0].line.find("TestBody"), std::string::npos) << records[0].line;
    EXPECT_EQ(records[0].line.find('\n'), std::string::npos);
    EXPECT_EQ(records[0].thread, std::this_thread::get_id());
}

/** @brief Tests that log_write hands its line to the sink unchanged and unfiltered. */
TEST_F(Medium_LoggerTest, LogWriteBypassesTheLevel)
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
TEST_F(Medium_LoggerTest, EmptySinkRestoresTheDefault)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));
    SR_LOG_ERROR(K_ALPHA_LOG_CATEGORY, "to the test sink");
    ASSERT_EQ(records.size(), 1u);

    set_log_sink(nullptr);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    SR_LOG_ERROR(K_ALPHA_LOG_CATEGORY, "restored {}", 1);
    const std::string standard_error = testing::internal::GetCapturedStderr();
    const std::string standard_output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(records.size(), 1u);
    EXPECT_NE(standard_error.find("restored 1\n"), std::string::npos) << standard_error;
    EXPECT_TRUE(standard_output.empty()) << standard_output;
}

/** @brief Tests that a record whose sink throws is written to stderr and the call returns. */
TEST_F(Medium_LoggerTest, ThrowingSinkLeavesTheRecordOnStderr)
{
    set_log_sink([](LogLevel, std::string_view)
    {
        throw std::runtime_error("sink failure");
    });

    testing::internal::CaptureStderr();
    EXPECT_NO_THROW({ SR_LOG_ERROR(K_ALPHA_LOG_CATEGORY, "kept {}", 2); });
    const std::string standard_error = testing::internal::GetCapturedStderr();

    EXPECT_NE(standard_error.find("sink failure"), std::string::npos) << standard_error;
    EXPECT_NE(standard_error.find("kept 2\n"), std::string::npos) << standard_error;
}

/** @brief Tests that asynchronous records run the sink on another thread, in order. */
TEST_F(Medium_LoggerTest, AsynchronousRecordsArriveBeforeSynchronousModeReturns)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_synchronous(false);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "record {}", 0);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "record {}", 1);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "record {}", 2);
    set_log_synchronous(true);

    ASSERT_EQ(records.size(), 3u);
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const std::string expected = "record " + std::to_string(i);
        EXPECT_NE(records[i].line.find(expected), std::string::npos) << records[i].line;
        EXPECT_NE(records[i].thread, std::this_thread::get_id());
    }
}

/** @brief Tests that each category keeps its own level, which set_log_level(level) leaves. */
TEST_F(Medium_LoggerTest, EachCategoryKeepsItsOwnLevel)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_level(K_BETA_LOG_CATEGORY, LogLevel::Debug);
    set_log_level(LogLevel::Error);
    EXPECT_EQ(log_level(K_ALPHA_LOG_CATEGORY), LogLevel::Error);
    EXPECT_EQ(log_level(K_BETA_LOG_CATEGORY), LogLevel::Debug);

    SR_LOG_DEBUG(K_ALPHA_LOG_CATEGORY, "alpha below its level");
    SR_LOG_DEBUG(K_BETA_LOG_CATEGORY, "beta at its level");

    ASSERT_EQ(records.size(), 1u);
    EXPECT_NE(records[0].line.find("[logger_test_beta]"), std::string::npos) << records[0].line;
    EXPECT_NE(records[0].line.find("[DEBUG]"), std::string::npos) << records[0].line;
}

/** @brief Tests that a full queue drops records and reports how many before the next one. */
TEST_F(Medium_LoggerTest, AFullQueueReportsTheRecordsItDropped)
{
    constexpr std::size_t K_QUEUE_CAPACITY = 4096;
    constexpr std::size_t K_DROPPED = 5;

    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    SeenRecords records;
    set_log_sink([&](LogLevel level, std::string_view line)
    {
        std::unique_lock<std::mutex> lock(mutex);
        records.push_back(SeenRecord{level, std::string(line), std::this_thread::get_id()});
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&]() { return released; });
    });

    set_log_synchronous(false);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "held in the sink");
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&]() { return entered; });
    }
    for (std::size_t i = 0; i < K_QUEUE_CAPACITY + K_DROPPED; ++i)
        SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "queued {}", i);
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
    }
    changed.notify_all();
    set_log_synchronous(true);

    ASSERT_EQ(records.size(), 2 + K_QUEUE_CAPACITY);
    EXPECT_EQ(records[1].level, LogLevel::Warn);
    EXPECT_NE(records[1].line.find("dropped 5 records"), std::string::npos) << records[1].line;
    EXPECT_NE(records[1].line.find("[logging]"), std::string::npos) << records[1].line;
    EXPECT_NE(records[2].line.find("queued 0"), std::string::npos) << records[2].line;
}

/** @brief Tests that a FATAL record reaches the sink after every queued one, on the caller. */
TEST_F(Medium_LoggerTest, FatalIsDeliveredAfterTheQueueOnTheCallingThread)
{
    SeenRecords records;
    set_log_sink(collect_into(&records));

    set_log_synchronous(false);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "queued {}", 0);
    SR_LOG_WARN(K_ALPHA_LOG_CATEGORY, "queued {}", 1);
    SR_LOG_FATAL(K_ALPHA_LOG_CATEGORY, "cannot continue");

    ASSERT_EQ(records.size(), 3u);
    EXPECT_NE(records[0].line.find("queued 0"), std::string::npos) << records[0].line;
    EXPECT_NE(records[1].line.find("queued 1"), std::string::npos) << records[1].line;
    EXPECT_EQ(records[2].level, LogLevel::Fatal);
    EXPECT_NE(records[2].line.find("[FATAL]"), std::string::npos) << records[2].line;
    EXPECT_EQ(records[2].thread, std::this_thread::get_id());
}

namespace
{
    /** @brief Sets an environment variable of this process. */
    void set_environment(const char* name, const char* value)
    {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }
}

/** @brief Tests that SR_LOG_LEVEL sets every category, then the categories it names. */
TEST(Medium_LoggerEnvironment, TheVariableSetsEveryCategoryThenTheNamedOnes)
{
    // A new process, so the level table reads the variable set here.
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(
        {
            set_environment("SR_LOG_LEVEL", "error, logger_test_beta=DEBUG");
            const bool read = log_level(K_ALPHA_LOG_CATEGORY) == LogLevel::Error &&
                              log_level(K_BETA_LOG_CATEGORY) == LogLevel::Debug &&
                              log_level() == LogLevel::Error;
            std::exit(read ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
}

/** @brief Tests that a value the logger cannot read is reported and leaves every level at WARN. */
TEST(Medium_LoggerEnvironment, AnUnreadableValueLeavesEveryLevelAtWarn)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(
        {
            set_environment("SR_LOG_LEVEL", "error,logger_test_beta=loud");
            const bool kept = log_level(K_ALPHA_LOG_CATEGORY) == LogLevel::Warn &&
                              log_level(K_BETA_LOG_CATEGORY) == LogLevel::Warn;
            std::exit(kept ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "is not a level");
}
