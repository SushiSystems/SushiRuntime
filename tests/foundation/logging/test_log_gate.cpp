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

#include <SushiRuntime/log/log_category.hpp>
#include <SushiRuntime/log/log_level.hpp>
#include <SushiRuntime/logging/log_gate.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the category the gate tests read. */
    constexpr LogCategory K_GATE_LOG_CATEGORY = make_log_category("log_gate_test");
}

/** @brief Tests the floor: Error with logging off, Debug in a release build, Trace otherwise. */
TEST(Small_LogGate, TheFloorFollowsTheBuild)
{
#if !SR_ENABLE_LOGGING
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, LogLevel::Error);
#elif defined(NDEBUG)
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, LogLevel::Debug);
#else
    EXPECT_EQ(K_LOG_LEVEL_FLOOR, LogLevel::Trace);
#endif
    static_assert(K_LOG_LEVEL_FLOOR <= LogLevel::Error, "ERROR and FATAL are never compiled out");
}

/** @brief Tests that a level passes the gate when it is at or above its category's level. */
TEST(Small_LogGate, ALevelPassesAtOrAboveItsCategoryLevel)
{
    const LogLevel saved = log_level();
    set_log_level(LogLevel::Warn);
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Fatal));
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Error));
    EXPECT_TRUE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Warn));
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Info));
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Trace));
    set_log_level(LogLevel::Off);
    EXPECT_FALSE(Detail::is_log_enabled(K_GATE_LOG_CATEGORY, LogLevel::Fatal));
    set_log_level(saved);
}

/** @brief Tests that a category's slot keeps its address, so a call site may keep it. */
TEST(Small_LogGate, ACategorySlotKeepsItsAddress)
{
    EXPECT_EQ(&Detail::log_level_slot(K_GATE_LOG_CATEGORY),
              &Detail::log_level_slot(make_log_category("log_gate_test")));
}
