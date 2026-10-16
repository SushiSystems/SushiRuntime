/**************************************************************************/
/* test_log_project.cpp                                                   */
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
 * @file test_log_project.cpp
 * @brief Tests LogProject: its name, its variable and the level each project keeps.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_project.hpp>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_category.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names one project these tests set. */
    constexpr LogProject K_FIRST_TEST_LOG_PROJECT{"log_project_first", "SR_TEST_UNSET_FIRST"};

    /** @brief Names a second project, which the first must not change. */
    constexpr LogProject K_SECOND_TEST_LOG_PROJECT{"log_project_second", "SR_TEST_UNSET_SECOND"};
}

/** @brief Tests that the level set for a project is read back for it and its categories. */
TEST(Small_LogProject, TheLevelSetIsTheLevelRead)
{
    const LogLevel saved = log_level(K_FIRST_TEST_LOG_PROJECT);
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                                 LogLevel::Error, LogLevel::Fatal, LogLevel::Off})
    {
        set_log_level(K_FIRST_TEST_LOG_PROJECT, level);
        EXPECT_EQ(log_level(K_FIRST_TEST_LOG_PROJECT), level);
        EXPECT_EQ(log_level(make_log_category(K_FIRST_TEST_LOG_PROJECT, "any")), level);
    }
    set_log_level(K_FIRST_TEST_LOG_PROJECT, saved);
    EXPECT_EQ(log_level(K_FIRST_TEST_LOG_PROJECT), saved);
}

/** @brief Tests that setting one project leaves another, even with a module of the same name. */
TEST(Small_LogProject, OneProjectLeavesAnother)
{
    const LogCategory first = make_log_category(K_FIRST_TEST_LOG_PROJECT, "memory");
    const LogCategory second = make_log_category(K_SECOND_TEST_LOG_PROJECT, "memory");
    const LogLevel saved_first = log_level(K_FIRST_TEST_LOG_PROJECT);
    const LogLevel saved_second = log_level(K_SECOND_TEST_LOG_PROJECT);

    set_log_level(K_SECOND_TEST_LOG_PROJECT, LogLevel::Warn);
    set_log_level(K_FIRST_TEST_LOG_PROJECT, LogLevel::Trace);
    EXPECT_EQ(log_level(first), LogLevel::Trace);
    EXPECT_EQ(log_level(second), LogLevel::Warn);

    set_log_level(K_FIRST_TEST_LOG_PROJECT, saved_first);
    set_log_level(K_SECOND_TEST_LOG_PROJECT, saved_second);
}
