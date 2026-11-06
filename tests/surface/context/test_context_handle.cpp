/**************************************************************************/
/* test_context_handle.cpp                                                */
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
 * @file test_context_handle.cpp
 * @brief Tests what a ContextHandle that shares no context reports.
 * @see tests/surface/context/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/context/context_handle.hpp>

using SushiRuntime::ContextHandle;

/** @brief Tests that a copy of a default handle shares no context, as the handle does. */
TEST(Small_ContextHandle, ACopyOfADefaultHandleIsEmpty)
{
    const ContextHandle handle;
    const ContextHandle copy = handle;

    EXPECT_FALSE(static_cast<bool>(handle));
    EXPECT_FALSE(static_cast<bool>(copy));
}
