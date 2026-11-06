/**************************************************************************/
/* test_device_pass.cpp                                                   */
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
 * @file test_device_pass.cpp
 * @brief Tests that compiled_for_device_v answers whether the unit compiles for the device.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/device_pass.hpp>

using namespace SushiRuntime;

namespace
{
    /**
     * @brief Says whether the build compiles this unit with -fsycl.
     *
     * The build gives -fsycl to the units in DEVICE_SOURCES of tests/surface/api/CMakeLists.txt,
     * and test_device_pass.cpp is not one of them.
     */
    constexpr bool K_UNIT_COMPILED_WITH_FSYCL = false;
} // namespace

/** @brief Tests that the answer is false in a unit the build compiles without -fsycl. */
TEST(Small_DevicePass, IsFalseInAUnitBuiltWithoutFsycl)
{
    EXPECT_EQ(Detail::compiled_for_device_v<int>, K_UNIT_COMPILED_WITH_FSYCL);
}
