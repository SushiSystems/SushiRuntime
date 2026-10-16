/**************************************************************************/
/* test_execution_log_category.cpp                                        */
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
 * @file test_execution_log_category.cpp
 * @brief Tests K_EXECUTION_LOG_CATEGORY: the name its records carry.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/execution/execution_log_category.hpp>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that the category is named after the module's folder. */
TEST(Small_ExecutionLogCategory, TheCategoryIsNamedAfterTheModule)
{
    static_assert(K_EXECUTION_LOG_CATEGORY.name() == "execution", "the folder name of the module");
    SUCCEED();
}
