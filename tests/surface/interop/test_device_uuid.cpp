/**************************************************************************/
/* test_device_uuid.cpp                                                   */
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
 * @file test_device_uuid.cpp
 * @brief Tests DeviceUUID: sixteen zero bytes by default, equal when every byte is.
 * @see tests/surface/interop/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/interop/device_uuid.hpp>

using namespace SushiRuntime;

/** @brief Tests that two UUIDs are equal only when every byte is. */
TEST(Unit_DeviceUUID, EqualOnlyWhenEveryByteIs)
{
    const DeviceUUID zero;
    EXPECT_EQ(zero.bytes.size(), 16u);
    for (const auto byte : zero.bytes)
        EXPECT_EQ(byte, 0u);
    for (std::size_t index = 0; index < zero.bytes.size(); ++index)
    {
        DeviceUUID other;
        other.bytes[index] = 1;
        EXPECT_NE(zero, other) << "byte " << index;
    }
    EXPECT_EQ(zero, DeviceUUID{});
}
