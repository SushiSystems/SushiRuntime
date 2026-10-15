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
