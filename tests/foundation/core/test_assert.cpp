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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_assert.cpp
 * @brief Tests that each assertion macro stops the process and names its site, and the handler slot.
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

    /** @brief Holds the last failure that record_failure saw. */
    SushiRuntime::AssertionFailure recorded_failure;

    /** @brief Counts the calls of record_failure. */
    int recorded_call_count = 0;

    /** @brief Writes a marker and the failure's line, in place of the default handler. */
    void write_marker(const SushiRuntime::AssertionFailure& failure)
    {
        std::fprintf(stderr, "replaced handler saw line %d\n", failure.line);
        std::fflush(stderr);
    }

    /** @brief Keeps the failure and counts the call, in place of the default handler. */
    void record_failure(const SushiRuntime::AssertionFailure& failure)
    {
        recorded_failure = failure;
        ++recorded_call_count;
    }

    TEST(Medium_AssertDeathTest, AssertStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_ASSERT(1 + 1 == 3, "the sum holds"), "the sum holds.*" + site(line));
    }

    TEST(Medium_AssertDeathTest, VerifyStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_VERIFY(1 + 1 == 3, "the sum holds"), "1 \\+ 1 == 3.*" + site(line));
    }

    TEST(Medium_AssertDeathTest, FatalStopsTheProcessAndNamesItsSite)
    {
        const int line = __LINE__ + 1;
        EXPECT_DEATH(SR_FATAL("the unreachable state"), "the unreachable state.*" + site(line));
    }

    TEST(Small_Assert, ATrueConditionLetsTheProcessContinue)
    {
        int evaluations = 0;
        SR_VERIFY(++evaluations == 1, "the condition runs once");
        SR_ASSERT(++evaluations == 2, "the condition runs once");
        EXPECT_EQ(evaluations, 2);
    }

    TEST(Medium_AssertDeathTest, TheProcessWideHandlerIsCalledBeforeTheProcessStops)
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

    /** @brief Tests that a report passes the whole failure to the held handler and returns. */
    TEST(Small_AssertionHandlerSlot, AReportCallsTheHeldHandlerWithTheWholeFailureAndReturns)
    {
        SushiRuntime::AssertionHandlerSlot slot;
        slot.exchange(&record_failure);
        recorded_call_count = 0;

        slot.report(SushiRuntime::AssertionFailure{"a == b", "the pair holds", "pair.cpp", 17});

        EXPECT_EQ(recorded_call_count, 1);
        EXPECT_STREQ(recorded_failure.expression, "a == b");
        EXPECT_STREQ(recorded_failure.message, "the pair holds");
        EXPECT_STREQ(recorded_failure.file, "pair.cpp");
        EXPECT_EQ(recorded_failure.line, 17);
    }

    /** @brief Tests that exchange() hands back the handler it replaces. */
    TEST(Small_AssertionHandlerSlot, ExchangeReturnsTheHandlerItReplaces)
    {
        SushiRuntime::AssertionHandlerSlot slot;
        slot.exchange(&write_marker);

        EXPECT_EQ(slot.exchange(&record_failure), &write_marker);
    }

    /** @brief Tests that exchanging null puts the default handler back. */
    TEST(Small_AssertionHandlerSlot, ExchangingNullRestoresTheDefaultHandler)
    {
        SushiRuntime::AssertionHandlerSlot slot;
        const SushiRuntime::AssertionHandler default_handler = slot.exchange(&write_marker);

        slot.exchange(nullptr);

        EXPECT_EQ(slot.exchange(&write_marker), default_handler);
        EXPECT_NE(default_handler, nullptr);
    }
} // namespace
