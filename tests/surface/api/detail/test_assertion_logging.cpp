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
 * @file test_assertion_logging.cpp
 * @brief Tests that a failed check reaches SushiLog at FATAL before the process stops.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>

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

    /** @brief Installs the logging handler twice, then fails a check. */
    void fail_a_check_with_logging_installed_twice()
    {
        SushiRuntime::Detail::install_fatal_logging();
        SushiRuntime::Detail::install_fatal_logging();
        SR_VERIFY(1 + 1 == 3, "the sum holds");
    }

    /** @brief Returns how many times @p needle occurs in @p text without overlapping. */
    std::size_t occurrences(const std::string& text, const std::string& needle)
    {
        std::size_t count = 0;
        for (std::size_t at = text.find(needle); at != std::string::npos;
             at = text.find(needle, at + needle.size()))
            ++count;
        return count;
    }

    /** @brief Matches a text that holds one string exactly once. */
    class ContainsExactlyOnce final : public ::testing::MatcherInterface<const std::string&>
    {
        public:
            /** @brief Builds a matcher for @p needle. */
            explicit ContainsExactlyOnce(std::string needle) : needle_(std::move(needle))
            {

            }

            /** @brief Returns whether @p text holds the string exactly once. */
            bool MatchAndExplain(const std::string& text,
                                 ::testing::MatchResultListener* listener) const override
            {
                const std::size_t count = occurrences(text, needle_);
                *listener << "holds it " << count << " times";
                return count == 1u;
            }

            /** @brief Writes what a match means. */
            void DescribeTo(std::ostream* out) const override
            {
                *out << "holds \"" << needle_ << "\" exactly once";
            }

            /** @brief Writes what a mismatch means. */
            void DescribeNegationTo(std::ostream* out) const override
            {
                *out << "does not hold \"" << needle_ << "\" exactly once";
            }

        private:
            /** @brief The string the text must hold once. */
            std::string needle_;
    };
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

/** @brief Tests that a second installation leaves the failed check logged once. */
TEST(Medium_AssertDeathTest, ASecondInstallationLogsTheFailedCheckOnce)
{
    EXPECT_DEATH(fail_a_check_with_logging_installed_twice(), ::testing::Matcher<const std::string&>(new ContainsExactlyOnce("[FATAL]")));
}
