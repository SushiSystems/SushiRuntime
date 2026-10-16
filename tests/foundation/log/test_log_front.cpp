/**************************************************************************/
/* test_log_front.cpp                                                     */
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
 * @file test_log_front.cpp
 * @brief Tests log_front.hpp: the floor a front compiles with and a front's macro end to end.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_front.hpp>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/log/log_project.hpp>
#include <SushiRuntime/log/log_record.hpp>
#include <SushiRuntime/log/log_sink.hpp>

using namespace SushiRuntime;

/** @brief Stands for a front's floor, as SB_LOG_FLOOR would be. */
#define FRONT_TEST_LOG_FLOOR SushiRuntime::log_level_floor(true)

/** @brief Stands for a front's WARN macro, as SB_LOG_WARN would be. */
#define FRONT_TEST_LOG_WARN(category, ...)                                                       \
    SR_LOG_FRONT_AT(FRONT_TEST_LOG_FLOOR, SushiRuntime::LogLevel::Warn, category, __VA_ARGS__)

/** @brief Stands for a front's INFO macro. */
#define FRONT_TEST_LOG_INFO(category, ...)                                                       \
    SR_LOG_FRONT_AT(FRONT_TEST_LOG_FLOOR, SushiRuntime::LogLevel::Info, category, __VA_ARGS__)

namespace
{
    /** @brief Names the project of the front these tests stand for. */
    constexpr LogProject K_FRONT_TEST_LOG_PROJECT{"front_test", "FRONT_TEST_LOG_LEVEL"};

    /** @brief Names the one module of that front. */
    constexpr LogCategory K_FRONT_TEST_LOG_CATEGORY =
        make_log_category(K_FRONT_TEST_LOG_PROJECT, "kernel");
}

/** @brief Tests that the floor is Error with logging off and below Error with it on. */
TEST(Small_LogFront, TheFloorFollowsTheSwitch)
{
    static_assert(log_level_floor(false) == LogLevel::Error, "off keeps ERROR and FATAL");
    static_assert(log_level_floor(true) <= LogLevel::Debug, "on keeps DEBUG at least");
    SUCCEED();
}

/** @brief Tests that a front's macro reaches the sink under its project and module, and that
 *         its project's level drops what is below it. */
TEST(Small_LogFront, AFrontLogsUnderItsProject)
{
    std::vector<std::string> categories;
    std::vector<std::string> messages;
    set_log_synchronous(true);
    set_log_sink([&categories, &messages](const LogRecord& record)
    {
        categories.push_back(std::string(record.project) + "." + std::string(record.module));
        messages.push_back(std::string(record.message));
    });
    const LogLevel saved = log_level(K_FRONT_TEST_LOG_PROJECT);
    set_log_level(K_FRONT_TEST_LOG_PROJECT, LogLevel::Warn);

    FRONT_TEST_LOG_INFO(K_FRONT_TEST_LOG_CATEGORY, "dropped {}", 1);
    FRONT_TEST_LOG_WARN(K_FRONT_TEST_LOG_CATEGORY, "kept {}", 2);

    set_log_sink(nullptr);
    set_log_level(K_FRONT_TEST_LOG_PROJECT, saved);
    set_log_synchronous(false);
    EXPECT_EQ(categories, std::vector<std::string>{"front_test.kernel"});
    EXPECT_EQ(messages, std::vector<std::string>{"kept 2"});
}
