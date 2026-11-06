/**************************************************************************/
/* test_node_label.cpp                                                    */
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
 * @file test_node_label.cpp
 * @brief Tests NodeLabel: a key and a value that copy as plain strings.
 * @author Mustafa Garip
 * @see tests/foundation/machine/README.md
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/node_label.hpp>

using namespace SushiRuntime;

/** @brief Tests that a copy keeps the key and the value. */
TEST(Small_NodeLabel, ACopyKeepsTheKeyAndTheValue)
{
    const NodeLabel label{"zone", "north"};
    const NodeLabel copy = label;
    EXPECT_EQ(copy.key, "zone");
    EXPECT_EQ(copy.value, "north");
}
