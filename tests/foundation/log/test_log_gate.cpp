/**************************************************************************/
/* test_log_gate.cpp                                                      */
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
 * @file test_log_gate.cpp
 * @brief Tests the log gate: the floor of the build and the levels it lets through.
 * @see tests/foundation/logging/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_category.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_project.hpp>
#include <SushiRuntime/log/log_gate.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the category the gate tests read. */
    /** @brief Names the project of the category the gate tests read. */
    constexpr SushiLog::LogProject K_GATE_TEST_LOG_PROJECT{"log_gate_test", "SR_LOG_GATE_TEST_LEVEL"};

    /** @brief Names the category the gate tests read. */
    constexpr SushiLog::LogCategory K_GATE_LOG_CATEGORY = SushiLog::make_log_category(K_GATE_TEST_LOG_PROJECT, "gate");
}

/** @brief Tests the floor: Error with logging off, Debug in a release build, Trace otherwise. */
TEST(Small_LogGate, TheFloorFollowsTheBuild)
{
#if !SR_ENABLE_LOGGING
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, SushiLog::LogLevel::Error);
#elif defined(NDEBUG)
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, SushiLog::LogLevel::Debug);
#else
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, SushiLog::LogLevel::Trace);
#endif
    static_assert(K_LOG_LEVEL_FLOOR <= SushiLog::LogLevel::Error, "ERROR and FATAL are never compiled out");
}

/** @brief Tests that a level passes the gate when it is at or above its category's level. */
TEST(Small_LogGate, ALevelPassesAtOrAboveItsCategoryLevel)
{
    const SushiLog::LogLevel saved = SushiLog::log_level(K_GATE_TEST_LOG_PROJECT);
    SushiLog::set_log_level(K_GATE_TEST_LOG_PROJECT, SushiLog::LogLevel::Warn);
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Fatal));
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Error));
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Warn));
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Info));
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Trace));
    SushiLog::set_log_level(K_GATE_TEST_LOG_PROJECT, SushiLog::LogLevel::Off);
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, SushiLog::LogLevel::Fatal));
    SushiLog::set_log_level(K_GATE_TEST_LOG_PROJECT, saved);
}

