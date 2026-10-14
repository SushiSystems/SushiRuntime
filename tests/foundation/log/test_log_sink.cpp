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
 * @brief Tests LogSink: the callable it holds, and that a sink can be installed and removed.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/log/log_sink.hpp>

using namespace SushiRuntime;

/** @brief Tests that a sink is called with the level and the line it was given. */
TEST(Unit_LogSink, ASinkReceivesTheLevelAndTheLine)
{
    LogLevel seen = LogLevel::Off;
    std::string line;
    const LogSink sink([&seen, &line](LogLevel level, std::string_view text)
    {
        seen = level;
        line = std::string(text);
    });
    sink(LogLevel::Warn, "fell back");
    EXPECT_EQ(seen, LogLevel::Warn);
    EXPECT_EQ(line, "fell back");
}

/** @brief Tests that a sink is installed, replaced by the default, and synchronous mode set. */
TEST(Unit_LogSink, ASinkIsInstalledAndRemoved)
{
    set_log_synchronous(true);
    set_log_sink(LogSink([](LogLevel, std::string_view) {}));
    set_log_sink(nullptr);
    set_log_synchronous(false);
    SUCCEED();
}
