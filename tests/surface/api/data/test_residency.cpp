/**************************************************************************/
/* test_residency.cpp                                                     */
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
 * @file test_residency.cpp
 * @brief Tests the rule that says which residencies the host may index directly.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/residency.hpp>

using SushiRuntime::Residency;
using SushiRuntime::Detail::is_host_addressable;

namespace
{
    TEST(Small_Residency, SharedAndHostMemoryAreHostAddressable)
    {
        EXPECT_TRUE(is_host_addressable(Residency::Shared));
        EXPECT_TRUE(is_host_addressable(Residency::Host));
    }

    TEST(Small_Residency, DeviceMemoryIsNotHostAddressable)
    {
        EXPECT_FALSE(is_host_addressable(Residency::Device));
    }
} // namespace
