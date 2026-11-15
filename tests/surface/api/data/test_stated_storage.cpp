/**************************************************************************/
/* test_stated_storage.cpp                                                */
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
 * @file test_stated_storage.cpp
 * @brief Tests StatedStorage, the storage of memory pinned to one stated device.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/stated_storage.hpp>
#include <SushiRuntime/region/liveness.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief The address of the block every test below builds; no test dereferences it. */
    void* const K_ADDRESS = reinterpret_cast<void*>(4096);

    /** @brief Returns a block of 64 bytes at K_ADDRESS that releases nothing. */
    Detail::MemoryBlock made_up_block()
    {
        return Detail::MemoryBlock{Detail::Allocation(K_ADDRESS, 64, nullptr)};
    }

    TEST(Small_StatedStorage, StatesTheDeviceItWasGiven)
    {
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{3}, Residency::Device);

        EXPECT_EQ(storage.device_index(), DeviceIndex{3});
    }

    TEST(Small_StatedStorage, ReportsTheAddressOfItsBlockAndTheResidencyItWasGiven)
    {
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{0}, Residency::Host);

        EXPECT_EQ(storage.address(), K_ADDRESS);
        EXPECT_EQ(storage.residency(), Residency::Host);
    }

    TEST(Small_StatedStorage, DeviceResidentMemoryIsNotHostAddressable)
    {
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{0}, Residency::Device);

        EXPECT_FALSE(storage.host_addressable());
    }

    TEST(Small_StatedStorage, SharedMemoryIsHostAddressable)
    {
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{0}, Residency::Shared);

        EXPECT_TRUE(storage.host_addressable());
    }

    TEST(Small_StatedStorage, TheHostRefusalNamesADeviceResidentBuffer)
    {
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{0}, Residency::Device);

        EXPECT_STREQ(storage.host_refusal(), "a device-resident buffer is not host-addressable; ");
    }

    TEST(Small_StatedStorage, BorrowedMemoryObservesItsOwnerUntilTheOwnerExpires)
    {
        LivenessToken owner = make_liveness();
        const Detail::StatedStorage storage(made_up_block(), DeviceIndex{0}, Residency::Shared,
                                            LivenessObserver(owner));
        const bool alive_before = !storage.owner_life().expired();

        owner.reset();

        EXPECT_TRUE(alive_before);
        EXPECT_TRUE(storage.owner_life().expired());
    }
} // namespace
