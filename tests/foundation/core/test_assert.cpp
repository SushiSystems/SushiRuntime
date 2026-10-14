/**************************************************************************/
/* test_assert.cpp                                                        */
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
 * @file test_assert.cpp
 * @brief Tests that each assertion macro stops the process and names its file and line.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#undef SR_ENABLE_ASSERTIONS
#define SR_ENABLE_ASSERTIONS 1

#include <cstdio>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/assert.hpp>

namespace
{
    /** @brief Returns the pattern a failure written at the given line of this file matches. */
    std::string site(int line)
    {
        return "test_assert\\.cpp:" + std::to_string(line);
    }

    /** @brief Writes a marker and the failure's line, in place of the default handler. */
    void write_marker(const SushiRuntime::AssertionFailure& failure)
    {
        std::fprintf(stderr, "replaced handler saw line %d\n", failure.line);
        std::fflush(stderr);
    }

    TEST(Unit_Assert, AssertStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_ASSERT(1 + 1 == 3, "the sum holds"), "the sum holds.*" + site(line));
    }

    TEST(Unit_Assert, VerifyStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_VERIFY(1 + 1 == 3, "the sum holds"), "1 \\+ 1 == 3.*" + site(line));
    }

    TEST(Unit_Assert, FatalStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_FATAL("the unreachable state"), "the unreachable state.*" + site(line));
    }

    TEST(Unit_Assert, ATrueConditionLetsTheProcessContinue)
    {
        int evaluations = 0;
        SR_VERIFY(++evaluations == 1, "the condition runs once");
        SR_ASSERT(++evaluations == 2, "the condition runs once");
        EXPECT_EQ(evaluations, 2);
    }

    TEST(Unit_Assert, AReplacedHandlerIsCalledBeforeTheProcessStops)
    {
        const int line = __LINE__ + 5;
        EXPECT_DEATH(
            {
                SushiRuntime::set_assertion_handler(&write_marker);
                // A handler that returns does not keep the process alive.
                SR_FATAL("unused");
            },
            "replaced handler saw line " + std::to_string(line));
    }

    TEST(Unit_Assert, SettingAHandlerReturnsTheOneItReplaces)
    {
        const SushiRuntime::AssertionHandler original =
            SushiRuntime::set_assertion_handler(&write_marker);
        EXPECT_EQ(SushiRuntime::set_assertion_handler(nullptr), &write_marker);
        EXPECT_EQ(SushiRuntime::set_assertion_handler(original), original);
    }
} // namespace
