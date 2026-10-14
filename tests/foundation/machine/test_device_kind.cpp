/**************************************************************************/
/* test_device_kind.cpp                                                   */
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
 * @file test_device_kind.cpp
 * @brief Tests DeviceKind: four distinct classes in one byte.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_kind.hpp>

using namespace SushiRuntime;

/** @brief Tests the size and the distinct values of the enumeration. */
TEST(Unit_DeviceKind, FourDistinctClassesInOneByte)
{
    static_assert(std::is_same_v<std::underlying_type_t<DeviceKind>, std::uint8_t>, "one byte");
    EXPECT_NE(DeviceKind::CPU, DeviceKind::GPU);
    EXPECT_NE(DeviceKind::GPU, DeviceKind::Accelerator);
    EXPECT_NE(DeviceKind::Accelerator, DeviceKind::Other);
}
