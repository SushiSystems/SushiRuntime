/**************************************************************************/
/* test_log_category.cpp                                                  */
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
 * @file test_log_category.cpp
 * @brief Tests LogCategory: its project and module, and the level each category keeps.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_category.hpp>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_project.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the project of the categories these tests set. */
    constexpr LogProject K_CATEGORY_TEST_LOG_PROJECT{"log_category_test", "SR_TEST_UNSET_LEVEL"};

    /** @brief Names the category these tests set. */
    constexpr LogCategory K_CATEGORY_TEST_LOG_CATEGORY =
        make_log_category(K_CATEGORY_TEST_LOG_PROJECT, "pinned");
}

/** @brief Tests that a category holds its project and module, at compile time. */
TEST(Small_LogCategory, ACategoryHoldsItsProjectAndModule)
{
    static_assert(K_CATEGORY_TEST_LOG_CATEGORY.project().name() == "log_category_test",
                  "the project given");
    static_assert(K_CATEGORY_TEST_LOG_CATEGORY.module() == "pinned", "the module given");
    SUCCEED();
}

/** @brief Tests that a level set for one category is read back for it alone and outlives a
 *         project level set later. */
TEST(Small_LogCategory, TheLevelSetForACategoryIsReadBack)
{
    const LogLevel saved = log_level(K_CATEGORY_TEST_LOG_PROJECT);
    set_log_level(K_CATEGORY_TEST_LOG_PROJECT, LogLevel::Warn);
    set_log_level(K_CATEGORY_TEST_LOG_CATEGORY, LogLevel::Trace);
    EXPECT_EQ(log_level(K_CATEGORY_TEST_LOG_CATEGORY), LogLevel::Trace);
    EXPECT_EQ(log_level(make_log_category(K_CATEGORY_TEST_LOG_PROJECT, "other")), LogLevel::Warn);

    set_log_level(K_CATEGORY_TEST_LOG_PROJECT, LogLevel::Error);
    EXPECT_EQ(log_level(K_CATEGORY_TEST_LOG_CATEGORY), LogLevel::Trace);
    EXPECT_EQ(log_level(make_log_category(K_CATEGORY_TEST_LOG_PROJECT, "other")), LogLevel::Error);
    set_log_level(K_CATEGORY_TEST_LOG_PROJECT, saved);
}
