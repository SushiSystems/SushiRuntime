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

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/process_environment/environment.hpp>

using namespace SushiRuntime::Distributed;

/** @brief Tests that a set variable is returned and an unset one is nothing. */
TEST(Medium_EnvironmentTest, ReadsASetVariableAndReportsAnUnsetOne)
{
    const std::optional<std::string> path = read_environment("PATH");
    ASSERT_TRUE(path.has_value());
    EXPECT_FALSE(path->empty());

    EXPECT_FALSE(read_environment("SR_TEST_VARIABLE_THAT_NOBODY_SETS").has_value());
}
