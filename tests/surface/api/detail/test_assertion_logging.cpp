/**************************************************************************/
/* test_assertion_logging.cpp                                             */
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

/**
 * @file test_assertion_logging.cpp
 * @brief Tests that a failed check reaches SushiLog at FATAL before the process stops.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/assertion_logging.hpp>
#include <SushiRuntime/core/assert.hpp>

namespace
{
    /** @brief Installs the logging handler, then fails a check the way library code does. */
    void fail_a_check_with_logging_installed()
    {
        SushiRuntime::Detail::install_fatal_logging();
        SR_VERIFY(1 + 1 == 3, "the sum holds");
    }
} // namespace

/** @brief Tests that the child's standard error carries a FATAL record with the check text. */
TEST(Medium_AssertDeathTest, LogsTheFailedCheckAtFatal)
{
    EXPECT_DEATH(fail_a_check_with_logging_installed(),
                 "\\[FATAL\\].*1 \\+ 1 == 3 failed: the sum holds");
}

/** @brief Tests that the handler installed before the logging one still runs after it. */
TEST(Medium_AssertDeathTest, TheDefaultHandlerStillWritesItsLineAfterTheLog)
{
    EXPECT_DEATH(fail_a_check_with_logging_installed(),
                 "SushiRuntime: 1 \\+ 1 == 3 failed: the sum holds");
}

/** @brief Tests that a second installation changes nothing and does not stop the process. */
TEST(Small_AssertHandler, InstallingTwiceIsHarmless)
{
    SushiRuntime::Detail::install_fatal_logging();
    SushiRuntime::Detail::install_fatal_logging();
    SUCCEED();
}
