/**************************************************************************/
/* test_alias_storage.cpp                                                 */
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
 * @file test_alias_storage.cpp
 * @brief Tests AliasStorage, the storage of a view over another live buffer's memory.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/alias_storage.hpp>
#include <SushiRuntime/region/liveness.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief The address every test below views; no test dereferences it. */
    void* const K_ADDRESS = reinterpret_cast<void*>(4096);

    TEST(Small_AliasStorage, ViewsTheAddressDeviceAndResidencyItWasGiven)
    {
        const Detail::AliasStorage storage(K_ADDRESS, 64, DeviceIndex{2}, Residency::Device,
                                           LivenessObserver());

        EXPECT_EQ(storage.address(), K_ADDRESS);
        EXPECT_EQ(storage.device_index(), DeviceIndex{2});
        EXPECT_EQ(storage.residency(), Residency::Device);
    }

    TEST(Small_AliasStorage, ObservesTheViewedBufferUntilItExpires)
    {
        LivenessToken source = make_liveness();
        const Detail::AliasStorage storage(K_ADDRESS, 64, DeviceIndex{0}, Residency::Shared,
                                           LivenessObserver(source));
        const bool alive_before = !storage.owner_life().expired();

        source.reset();

        EXPECT_TRUE(alive_before);
        EXPECT_TRUE(storage.owner_life().expired());
    }
} // namespace
