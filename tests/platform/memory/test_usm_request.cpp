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
