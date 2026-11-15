/**************************************************************************/
/* test_device_key_assigner.cpp                                           */
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
 * @file test_device_key_assigner.cpp
 * @brief Tests DeviceKeyAssigner.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/device_key_assigner.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Returns a device with no id on a backend. */
    DeviceIdentity unnamed(DeviceBackend backend, const char* vendor, const char* name)
    {
        DeviceIdentity identity;
        identity.backend = backend;
        identity.vendor = vendor;
        identity.name = name;
        return identity;
    }
} // namespace

/** @brief Tests that a CUDA device with a UUID is named by it, before its PCI address. */
TEST(Small_DeviceKeyAssigner, AUUIDBeatsAPCIAddress)
{
    DeviceIdentity identity = unnamed(DeviceBackend::CUDA, "NVIDIA", "card");
    identity.uuid = HardwareID{1};
    identity.pci_address = PCIDomainAddress{0, 1, 0, 0};

    const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign({identity});

    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].id_kind, HardwareIDKind::UUID);
}

/** @brief Tests that an OpenCL device with a UUID and a PCI address is named by its address. */
TEST(Small_DeviceKeyAssigner, AnOpenCLDeviceIsNamedByItsPCIAddress)
{
    DeviceIdentity identity = unnamed(DeviceBackend::OpenCL, "NVIDIA", "card");
    identity.uuid = HardwareID{1};
    identity.pci_address = PCIDomainAddress{0, 1, 0, 0};

    const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign({identity});

    EXPECT_EQ(keys[0].id_kind, HardwareIDKind::PCIAddress);
}

/** @brief Tests that devices with no id take ordinals from the sorted order of their vendor and name. */
TEST(Small_DeviceKeyAssigner, DevicesWithNoIDTakeOrdinalsInNameOrder)
{
    const std::vector<DeviceIdentity> listed = {
        unnamed(DeviceBackend::OpenCL, "V", "card b"), unnamed(DeviceBackend::OpenCL, "V", "card a"),
        unnamed(DeviceBackend::OpenCL, "A", "card z")};

    const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign(listed);

    EXPECT_EQ(keys[2].ordinal, 0u);
    EXPECT_EQ(keys[1].ordinal, 1u);
    EXPECT_EQ(keys[0].ordinal, 2u);
}

/** @brief Tests that ordinals count separately for each backend. */
TEST(Small_DeviceKeyAssigner, OrdinalsCountSeparatelyForEachBackend)
{
    const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign(
        {unnamed(DeviceBackend::OpenCL, "V", "card"), unnamed(DeviceBackend::Host, "V", "card")});

    EXPECT_EQ(keys[0].ordinal, 0u);
    EXPECT_EQ(keys[1].ordinal, 0u);
}

/** @brief Tests that two devices that report one UUID are told apart by ordinal. */
TEST(Small_DeviceKeyAssigner, DevicesThatShareAUUIDAreToldApartByOrdinal)
{
    DeviceIdentity first = unnamed(DeviceBackend::LevelZero, "Intel", "tile a");
    first.uuid = HardwareID{7};
    DeviceIdentity second = unnamed(DeviceBackend::LevelZero, "Intel", "tile b");
    second.uuid = HardwareID{7};

    const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign({second, first});

    EXPECT_EQ(keys[1].ordinal, 0u);
    EXPECT_EQ(keys[0].ordinal, 1u);
    EXPECT_NE(keys[0], keys[1]);
}

/** @brief Tests that the key of a device does not change when the input order does. */
TEST(Small_DeviceKeyAssigner, KeysDoNotDependOnTheInputOrder)
{
    std::vector<DeviceIdentity> listed = {
        unnamed(DeviceBackend::OpenCL, "V", "card a"), unnamed(DeviceBackend::OpenCL, "V", "card b"),
        unnamed(DeviceBackend::OpenCL, "W", "card c"), unnamed(DeviceBackend::Host, "H", "cpu")};
    std::vector<std::size_t> permutation = {0, 1, 2, 3};
    const std::vector<DeviceKey> reference = DeviceKeyAssigner::standard().assign(listed);

    do
    {
        std::vector<DeviceIdentity> shuffled;
        for (const std::size_t position : permutation)
            shuffled.push_back(listed[position]);
        const std::vector<DeviceKey> keys = DeviceKeyAssigner::standard().assign(shuffled);
        for (std::size_t index = 0; index < permutation.size(); ++index)
            EXPECT_EQ(keys[index], reference[permutation[index]]) << shuffled[index].name;
    } while (std::next_permutation(permutation.begin(), permutation.end()));
}
