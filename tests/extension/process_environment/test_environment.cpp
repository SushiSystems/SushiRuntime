/**************************************************************************/
/* test_environment.cpp                                                   */
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
 * @file test_environment.cpp
 * @brief Tests read_environment(): a set variable and an unset one.
 * @see tests/extension/process_environment/README.md
 * @author Mustafa Garip
 */

#include <cstdlib>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/process_environment/environment.hpp>

using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the variable the tests set and unset. */
    constexpr const char* K_VARIABLE = "SR_TEST_READ_ENVIRONMENT";

    /**
     * @brief Sets or removes one variable of this process and puts back what it found.
     */
    class ScopedVariable final
    {
        public:
            /**
             * @brief Sets @p name to @p value, or removes it when @p value is null.
             */
            ScopedVariable(const char* name, const char* value) : name_(name)
            {
                previous_ = read_environment(name);
                assign(value);
            }

            /** @brief Puts back the value the variable had, or removes it. */
            ~ScopedVariable()
            {
                assign(previous_.has_value() ? previous_->c_str() : nullptr);
            }

            ScopedVariable(const ScopedVariable&) = delete;
            ScopedVariable& operator=(const ScopedVariable&) = delete;

        private:
            /** @brief Writes the variable through the platform's call; null removes it. */
            void assign(const char* value) const
            {
#ifdef _WIN32
                _putenv_s(name_.c_str(), value == nullptr ? "" : value);
#else
                if (value == nullptr)
                    unsetenv(name_.c_str());
                else
                    setenv(name_.c_str(), value, 1);
#endif
            }

            /// @brief The variable's name.
            std::string name_;

            /// @brief The value the variable had before the guard, or nothing.
            std::optional<std::string> previous_;
    };
} // namespace

/** @brief Tests that a set variable is returned with its value. */
TEST(Medium_EnvironmentTest, ASetVariableIsReturnedWithItsValue)
{
    const ScopedVariable variable(K_VARIABLE, "sr-test-value");

    const std::optional<std::string> value = read_environment(K_VARIABLE);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "sr-test-value");
}

/** @brief Tests that an unset variable is nothing. */
TEST(Medium_EnvironmentTest, AnUnsetVariableIsNothing)
{
    const ScopedVariable variable(K_VARIABLE, nullptr);

    EXPECT_FALSE(read_environment(K_VARIABLE).has_value());
}
