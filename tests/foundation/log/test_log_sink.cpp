/**************************************************************************/
/* test_log_sink.cpp                                                      */
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
 * @file test_log_sink.cpp
 * @brief Tests LogSink: the record it receives, installing it, and log_forward_sink().
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_front.hpp>
#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/log/log_project.hpp>
#include <SushiRuntime/log/log_record.hpp>
#include <SushiRuntime/log/log_sink.hpp>

using namespace SushiRuntime;

/** @brief Logs one record through the runtime's logger, as a front's macro does. */
#define SINK_TEST_LOG(level, category, ...)                                                      \
    SR_LOG_FRONT_AT(SushiRuntime::log_level_floor(true), level, category, __VA_ARGS__)

/** @brief Tests that a sink is called with every field of the record it was given. */
TEST(Small_LogSink, ASinkReceivesEveryFieldOfTheRecord)
{
    LogRecord seen{};
    std::string text;
    const LogSink sink([&seen, &text](const LogRecord& record)
    {
        seen = record;
        text = std::string(record.text);
    });
    sink(LogRecord{LogLevel::Warn, "sushiblas", "blas", "gemm.cpp", 12, 3, 42, "fell back",
                   "the line"});
    EXPECT_EQ(seen.level, LogLevel::Warn);
    EXPECT_EQ(seen.project, "sushiblas");
    EXPECT_EQ(seen.module, "blas");
    EXPECT_EQ(seen.file, "gemm.cpp");
    EXPECT_EQ(seen.line, 12u);
    EXPECT_EQ(seen.thread, 3u);
    EXPECT_EQ(seen.time_ns, 42);
    EXPECT_EQ(seen.message, "fell back");
    EXPECT_EQ(text, "the line");
}

/** @brief Tests that a sink is installed, replaced by the default, and synchronous mode set. */
TEST(Small_LogSink, ASinkIsInstalledAndRemoved)
{
    set_log_synchronous(true);
    set_log_sink(LogSink([](const LogRecord&) {}));
    set_log_sink(nullptr);
    set_log_synchronous(false);
    SUCCEED();
}

namespace
{
    /** @brief Names the project of the records this logger makes itself in these tests. */
    constexpr LogProject K_FORWARD_TEST_LOG_PROJECT{"forward_test", "SR_FORWARD_TEST_LOG_LEVEL"};

    /** @brief Names the category of those records. */
    constexpr LogCategory K_FORWARD_TEST_LOG_CATEGORY =
        make_log_category(K_FORWARD_TEST_LOG_PROJECT, "own");

    /** @brief Holds a copy of every field a sink received. */
    struct CopiedRecord
    {
        LogLevel level;       /**< The level. */
        std::string project;  /**< The project. */
        std::string module;   /**< The module. */
        std::string file;     /**< The file. */
        std::uint32_t line;   /**< The line. */
        std::uint32_t thread; /**< The thread number. */
        std::int64_t time_ns; /**< The time. */
        std::string message;  /**< The message. */
        std::string text;     /**< The whole line. */
    };

    /** @brief Returns a sink that copies each record into @p records. */
    LogSink copy_into(std::vector<CopiedRecord>* records)
    {
        return [records](const LogRecord& record)
        {
            records->push_back(CopiedRecord{record.level, std::string(record.project),
                                            std::string(record.module), std::string(record.file),
                                            record.line, record.thread, record.time_ns,
                                            std::string(record.message),
                                            std::string(record.text)});
        };
    }

    /** @brief Returns a record as a second logger sends it: no thread number and no line. */
    LogRecord sent_record(LogLevel level, std::int64_t time_ns, const char* message)
    {
        return LogRecord{level, "sushidsp", "mixer", "mixer.cpp", 77, 0, time_ns, message, ""};
    }
}

/**
 * @brief Tests that a forwarded record keeps its fields and its order, takes the thread
 *        number and the line of this logger, and passes none of its levels.
 */
TEST(Medium_LogForward, AForwardedRecordKeepsItsFieldsAndOrder)
{
    std::vector<CopiedRecord> records;
    const LogLevel saved = log_level(K_FORWARD_TEST_LOG_PROJECT);
    set_log_synchronous(true);
    set_log_sink(copy_into(&records));
    set_log_level(K_FORWARD_TEST_LOG_PROJECT, LogLevel::Info);

    const LogSink forward = log_forward_sink();
    forward(sent_record(LogLevel::Debug, 1000000001, "first"));
    SINK_TEST_LOG(LogLevel::Info, K_FORWARD_TEST_LOG_CATEGORY, "own record");
    set_log_synchronous(false);
    forward(sent_record(LogLevel::Warn, 2000000002, "second"));
    forward(sent_record(LogLevel::Error, 3000000003, "third"));
    set_log_synchronous(true);

    set_log_sink(nullptr);
    set_log_level(K_FORWARD_TEST_LOG_PROJECT, saved);

    ASSERT_EQ(records.size(), 4u);
    const std::uint32_t own_thread = records[1].thread;
    EXPECT_EQ(records[1].message, "own record");

    const std::vector<std::size_t> forwarded{0, 2, 3};
    const std::vector<std::string> messages{"first", "second", "third"};
    const std::vector<LogLevel> levels{LogLevel::Debug, LogLevel::Warn, LogLevel::Error};
    const std::vector<std::int64_t> times{1000000001, 2000000002, 3000000003};
    for (std::size_t i = 0; i < forwarded.size(); ++i)
    {
        const CopiedRecord& record = records[forwarded[i]];
        EXPECT_EQ(record.level, levels[i]);
        EXPECT_EQ(record.project, "sushidsp");
        EXPECT_EQ(record.module, "mixer");
        EXPECT_EQ(record.file, "mixer.cpp");
        EXPECT_EQ(record.line, 77u);
        EXPECT_EQ(record.time_ns, times[i]);
        EXPECT_EQ(record.message, messages[i]);
        EXPECT_EQ(record.thread, own_thread);
        EXPECT_NE(record.text.find("[sushidsp.mixer] [t" + std::to_string(own_thread) +
                                   "] [mixer.cpp:77] " + messages[i]),
                  std::string::npos)
            << record.text;
    }
}
