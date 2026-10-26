/**************************************************************************/
/* test_process_environment_log_category.cpp                              */
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
 * @file test_process_environment_log_category.cpp
 * @brief Tests K_PROCESS_ENVIRONMENT_LOG_CATEGORY: the name its records carry.
 * @see tests/extension/process_environment/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/process_environment/process_environment_log_category.hpp>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that the category is the module's folder in the runtime's project. */
TEST(Small_ProcessEnvironmentLogCategory, TheCategoryIsNamedAfterTheModule)
{
    static_assert(K_PROCESS_ENVIRONMENT_LOG_CATEGORY.module() == "process_environment",
                  "the folder name of the module");
    static_assert(K_PROCESS_ENVIRONMENT_LOG_CATEGORY.project().name() == "sushiruntime",
                  "the project of the runtime");
    SUCCEED();
}
