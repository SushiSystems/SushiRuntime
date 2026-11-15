/**************************************************************************/
/* test_planned_storage.cpp                                               */
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
 * @file test_planned_storage.cpp
 * @brief Tests PlannedStorage, the storage of a buffer the runtime places.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/planned_storage.hpp>

using namespace SushiRuntime;

namespace
{
    TEST(Small_PlannedStorage, StatesNoDeviceNotEvenDeviceZero)
    {
        const Detail::PlannedStorage storage;

        EXPECT_EQ(storage.device_index(), std::nullopt);
    }

    TEST(Small_PlannedStorage, HoldsNoAddressAndIsPlanned)
    {
        const Detail::PlannedStorage storage;

        EXPECT_EQ(storage.address(), nullptr);
        EXPECT_EQ(storage.residency(), Residency::Planned);
    }

    TEST(Small_PlannedStorage, IsNotHostAddressableAndSaysWhy)
    {
        const Detail::PlannedStorage storage;

        EXPECT_FALSE(storage.host_addressable());
        EXPECT_STREQ(storage.host_refusal(), "a planned buffer has no address the host may hold; ");
    }
} // namespace
