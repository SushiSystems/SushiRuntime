/**************************************************************************/
/* test_logging_names.cpp                                                 */
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
 * @file test_logging_names.cpp
 * @brief Tests that the logging controls are reachable from the surface's logging header alone.
 * @see modules/surface/api/tests/runtime/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/api/runtime/logging.hpp>

static_assert(std::is_same<decltype(&SushiRuntime::set_log_synchronous), void (*)(bool)>::value,
              "set_log_synchronous(bool) must be declared by api/runtime/logging.hpp");
static_assert(std::is_same<decltype(&SushiRuntime::set_log_sink),
                           void (*)(SushiRuntime::LogSink)>::value,
              "set_log_sink(LogSink) must be declared by api/runtime/logging.hpp");

/** @brief Tests that a program switches delivery, the sink and the level with this header. */
TEST(Integration_LoggingNames, TheControlsAreCallableFromTheSurfaceHeader)
{
    const SushiRuntime::LogLevel saved = SushiRuntime::log_level();
    std::string seen;

    SushiRuntime::set_log_synchronous(true);
    SushiRuntime::set_log_sink([&seen](SushiRuntime::LogLevel, std::string_view line)
    {
        seen.assign(line);
    });
    SushiRuntime::set_log_level(SushiRuntime::LogLevel::Error);
    EXPECT_EQ(SushiRuntime::log_level(), SushiRuntime::LogLevel::Error);

    // No record is logged here, so the sink stays unused.
    EXPECT_TRUE(seen.empty());

    SushiRuntime::set_log_sink(SushiRuntime::LogSink());
    SushiRuntime::set_log_synchronous(false);
    SushiRuntime::set_log_level(saved);
}
