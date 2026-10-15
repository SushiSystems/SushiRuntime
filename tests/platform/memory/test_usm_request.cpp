/**************************************************************************/
/* test_usm_request.cpp                                                   */
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
 * @file test_usm_request.cpp
 * @brief Tests USMRequest: shared memory of no bytes on device 0 by default, and three kinds.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/usm_request.hpp>

using namespace SushiRuntime::Internal::Memory;

/** @brief Tests the defaults of a request. */
TEST(Small_USMRequest, DefaultsToSharedMemoryOnDeviceZero)
{
    const USMRequest request;
    EXPECT_EQ(request.kind, USMKind::Shared);
    EXPECT_EQ(request.bytes, 0u);
    EXPECT_EQ(request.alignment, 0u);
    EXPECT_EQ(request.device_index, 0u);
}

/** @brief Tests the three distinct kinds in one byte. */
TEST(Small_USMRequest, ThreeKindsInOneByte)
{
    static_assert(std::is_same_v<std::underlying_type_t<USMKind>, std::uint8_t>, "one byte");
    EXPECT_NE(USMKind::Host, USMKind::Shared);
    EXPECT_NE(USMKind::Shared, USMKind::Device);
    EXPECT_NE(USMKind::Host, USMKind::Device);
}
