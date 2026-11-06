/**************************************************************************/
/* test_placement.cpp                                                     */
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
 * @file test_placement.cpp
 * @brief Tests On: a device by index, or the device of the handle it names.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/placement.hpp>

using namespace SushiRuntime;

/** @brief Tests that an index names that device and observes nothing. */
TEST(Medium_Placement, AnIndexNamesItsDevice)
{
    const On placement(DeviceIndex{0});
    EXPECT_EQ(placement.device, DeviceIndex{0});
    EXPECT_TRUE(placement.life.expired());
}

/** @brief Tests that a buffer names its own device and is observed. */
TEST(Medium_Placement, ABufferNamesItsDevice)
{
    auto runtime = Runtime::create().value();
    auto buffer = runtime.buffer<int>(4).value();
    const On placement(buffer);
    EXPECT_EQ(placement.device, buffer.device_index());
    EXPECT_FALSE(placement.life.expired());
}
