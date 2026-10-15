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
 * @brief Tests the log gate: the ceiling of the build and the levels it lets through.
 * @see tests/foundation/logging/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/logging/log_gate.hpp>

using namespace SushiRuntime;

/** @brief Tests the ceiling: Info in a release build, Debug in a debug one. */
TEST(Small_LogGate, TheCeilingFollowsTheBuild)
{
#if !SR_ENABLE_LOGGING
    EXPECT_EQ(K_LOG_LEVEL_CEILING, LogLevel::Off);
#elif defined(NDEBUG)
    EXPECT_EQ(K_LOG_LEVEL_CEILING, LogLevel::Info);
#else
    EXPECT_EQ(K_LOG_LEVEL_CEILING, LogLevel::Debug);
#endif
}

/** @brief Tests that a level passes the gate when it is at or below the level set. */
TEST(Small_LogGate, ALevelPassesAtOrBelowTheLevelSet)
{
    const LogLevel saved = log_level();
    set_log_level(LogLevel::Warn);
    EXPECT_TRUE(Detail::is_log_enabled(LogLevel::Error) || K_LOG_LEVEL_CEILING == LogLevel::Off);
    EXPECT_TRUE(Detail::is_log_enabled(LogLevel::Warn) || K_LOG_LEVEL_CEILING == LogLevel::Off);
    EXPECT_FALSE(Detail::is_log_enabled(LogLevel::Info));
    set_log_level(LogLevel::Off);
    EXPECT_FALSE(Detail::is_log_enabled(LogLevel::Error));
    set_log_level(saved);
}
