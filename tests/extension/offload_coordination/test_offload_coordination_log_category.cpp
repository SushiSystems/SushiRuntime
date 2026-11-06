/**************************************************************************/
/* test_offload_coordination_log_category.cpp                             */
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
 * @file test_offload_coordination_log_category.cpp
 * @brief Tests K_OFFLOAD_COORDINATION_LOG_CATEGORY: the name its records carry.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/offload_coordination/offload_coordination_log_category.hpp>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that the category is the module's folder in the runtime's project. */
TEST(Small_OffloadCoordinationLogCategory, TheCategoryIsNamedAfterTheModule)
{
    static_assert(K_OFFLOAD_COORDINATION_LOG_CATEGORY.module() == "offload_coordination",
                  "the folder name of the module");
    static_assert(K_OFFLOAD_COORDINATION_LOG_CATEGORY.project().name() == "sushiruntime",
                  "the project of the runtime");
    SUCCEED();
}
