/**************************************************************************/
/* test_queue_flavour.cpp                                                 */
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
 * @file test_queue_flavour.cpp
 * @brief Tests QueueFlavour: equality over both fields.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/device/queue_flavour.hpp>

using SushiRuntime::Internal::Device::QueueFlavour;

/** @brief Tests that two flavours are equal only when both fields are. */
TEST(Small_QueueFlavour, EqualityComparesBothFields)
{
    EXPECT_EQ((QueueFlavour{true, true}), (QueueFlavour{true, true}));
    EXPECT_NE((QueueFlavour{true, false}), (QueueFlavour{false, false}));
    EXPECT_NE((QueueFlavour{false, true}), (QueueFlavour{false, false}));
    EXPECT_NE((QueueFlavour{true, false}), (QueueFlavour{false, true}));
}
