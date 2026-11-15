/**************************************************************************/
/* test_handle_location.cpp                                               */
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
 * @file test_handle_location.cpp
 * @brief Tests the memory space of each device and the lookup of a handle's device instance.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    static_assert(host_memory_space() == MemorySpaceID(0), "the host is space 0");
    static_assert(device_memory_space(0) == MemorySpaceID(1), "device i is space i + 1");
    static_assert(device_memory_space(3) == MemorySpaceID(4), "device i is space i + 1");

    /** @brief Mints a handle in @p directory with an instance at @p address on @p device. */
    GlobalHandle placed(Internal::Memory::ResidencyDirectory& directory, void* address, std::size_t device)
    {
        const MintOutcome minted = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(minted.status, HandleStatus::Ok);
        EXPECT_EQ(directory.set_instance(minted.handle, device_memory_space(device), address), HandleStatus::Ok);
        return minted.handle;
    }

    /** @brief Tests that locate() finds the lowest device that holds an instance. */
    TEST(Small_HandleLocation, LocateFindsTheLowestDeviceWithAnInstance)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int cell = 0;
        const GlobalHandle handle = placed(directory, &cell, 2);

        const std::optional<HandleLocation> location = locate(directory, handle, 4);

        ASSERT_TRUE(location.has_value());
        EXPECT_EQ(location->device, 2u);
        EXPECT_EQ(location->address, &cell);
        EXPECT_FALSE(locate(directory, handle, 2).has_value()) << "device 2 lies past two devices";
    }

    /** @brief Tests that the null handle and a retired handle have no location. */
    TEST(Small_HandleLocation, NullAndRetiredHandlesHaveNoLocation)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int cell = 0;
        const GlobalHandle handle = placed(directory, &cell, 0);
        EXPECT_EQ(directory.retire(handle), HandleStatus::Ok);

        EXPECT_FALSE(locate(directory, GlobalHandle::null(), 1).has_value());
        EXPECT_FALSE(locate(directory, handle, 1).has_value());
    }

    /** @brief Tests that resolve_on_device() gives the instance on that device and null elsewhere. */
    TEST(Small_HandleLocation, ResolveOnDeviceGivesThatDevicesInstance)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int cell = 0;
        const GlobalHandle handle = placed(directory, &cell, 1);

        EXPECT_EQ(resolve_on_device(directory, handle, 1), &cell);
        EXPECT_EQ(resolve_on_device(directory, handle, 0), nullptr);
    }
} // namespace
