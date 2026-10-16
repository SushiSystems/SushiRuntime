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
 * @brief Tests LogSink: SushiLog's sink under its SushiRuntime name, and installing it.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/log/log_record.hpp>
#include <SushiRuntime/log/log_sink.hpp>

using namespace SushiRuntime;

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

/** @brief Tests that the SushiRuntime name is SushiLog's sink type. */
TEST(Small_LogSink, TheSinkIsSushiLogs)
{
    static_assert(std::is_same<LogSink, SushiLog::LogSink>::value, "one sink type");
    SUCCEED();
}
