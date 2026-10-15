/**************************************************************************/
/* test_log_level.cpp                                                     */
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
 * @file test_log_level.cpp
 * @brief Tests LogLevel: the order of its values and the level the process keeps.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_level.hpp>

using namespace SushiRuntime;

/** @brief Tests that each level is more verbose than the one before, from Off to Debug. */
TEST(Small_LogLevel, LevelsRiseFromOffToDebug)
{
    static_assert(static_cast<int>(LogLevel::Off) == 0, "Off logs nothing");
    static_assert(static_cast<int>(LogLevel::Error) == 1, "Error is the first level that logs");
    static_assert(static_cast<int>(LogLevel::Warn) == 2, "Warn follows Error");
    static_assert(static_cast<int>(LogLevel::Info) == 3, "Info follows Warn");
    static_assert(static_cast<int>(LogLevel::Debug) == 4, "Debug is the most verbose");
    SUCCEED();
}

/** @brief Tests that set_log_level() is what log_level() returns, and restores the level. */
TEST(Small_LogLevel, TheLevelSetIsTheLevelRead)
{
    const LogLevel saved = log_level();
    for (const LogLevel level : {LogLevel::Off, LogLevel::Error, LogLevel::Warn, LogLevel::Info})
    {
        set_log_level(level);
        EXPECT_EQ(log_level(), level);
    }
    set_log_level(saved);
    EXPECT_EQ(log_level(), saved);
}
