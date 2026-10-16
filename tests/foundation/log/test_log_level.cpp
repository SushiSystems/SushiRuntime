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
 * @brief Tests LogLevel: the order of its values and the level every category takes.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_level.hpp>

using namespace SushiRuntime;

/** @brief Tests that each level is more severe than the one before, from Trace to Off. */
TEST(Small_LogLevel, LevelsRiseFromTraceToOff)
{
    static_assert(static_cast<int>(LogLevel::Trace) == 0, "Trace is the least severe");
    static_assert(static_cast<int>(LogLevel::Debug) == 1, "Debug follows Trace");
    static_assert(static_cast<int>(LogLevel::Info) == 2, "Info follows Debug");
    static_assert(static_cast<int>(LogLevel::Warn) == 3, "Warn follows Info");
    static_assert(static_cast<int>(LogLevel::Error) == 4, "Error follows Warn");
    static_assert(static_cast<int>(LogLevel::Fatal) == 5, "Fatal is the most severe record");
    static_assert(static_cast<int>(LogLevel::Off) == 6, "Off passes nothing");
    SUCCEED();
}

/** @brief Tests that set_log_level() is what log_level() returns, and restores the level. */
TEST(Small_LogLevel, TheLevelSetIsTheLevelRead)
{
    const LogLevel saved = log_level();
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                                 LogLevel::Error, LogLevel::Fatal, LogLevel::Off})
    {
        set_log_level(level);
        EXPECT_EQ(log_level(), level);
    }
    set_log_level(saved);
    EXPECT_EQ(log_level(), saved);
}
