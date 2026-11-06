/**************************************************************************/
/* test_sushiruntime_log_project.cpp                                      */
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
 * @file test_sushiruntime_log_project.cpp
 * @brief Tests K_SUSHIRUNTIME_LOG_PROJECT: the name and variable of the runtime's records.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/sushiruntime_log_project.hpp>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that the runtime's project is sushiruntime, read from SR_LOG_LEVEL. */
TEST(Small_SushiRuntimeLogProject, TheProjectIsTheRuntimeReadFromItsVariable)
{
    static_assert(K_SUSHIRUNTIME_LOG_PROJECT.name() == "sushiruntime", "the runtime's name");
    static_assert(K_SUSHIRUNTIME_LOG_PROJECT.level_variable() == "SR_LOG_LEVEL",
                  "the runtime's variable");
    SUCCEED();
}
