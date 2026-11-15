/**************************************************************************/
/* test_pci_device_key_source.cpp                                         */
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
 * @file test_pci_device_key_source.cpp
 * @brief Tests PCIDeviceKeySource.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/topology/pci_device_key_source.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that the key holds the domain, bus, device and function in bus order. */
TEST(Small_PCIDeviceKeySource, TheKeyHoldsTheAddressFieldsInBusOrder)
{
    DeviceIdentity identity;
    identity.backend = DeviceBackend::CUDA;
    identity.pci_address = PCIDomainAddress{0x01020304u, 0x0a, 0x0b, 0x2};

    const std::optional<DeviceKey> key = PCIDeviceKeySource().key_of(identity);

    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->backend, DeviceBackend::CUDA);
    EXPECT_EQ(key->id_kind, HardwareIDKind::PCIAddress);
    EXPECT_EQ(key->hardware_id[0], 0x01);
    EXPECT_EQ(key->hardware_id[3], 0x04);
    EXPECT_EQ(key->hardware_id[4], 0x0a);
    EXPECT_EQ(key->hardware_id[5], 0x0b);
    EXPECT_EQ(key->hardware_id[6], 0x02);
    EXPECT_EQ(key->hardware_id[7], 0x00);
}

/** @brief Tests that two addresses on one bus give keys that order by device. */
TEST(Small_PCIDeviceKeySource, KeysOrderByAddress)
{
    DeviceIdentity first;
    first.pci_address = PCIDomainAddress{0, 1, 0, 0};
    DeviceIdentity second;
    second.pci_address = PCIDomainAddress{0, 2, 0, 0};

    EXPECT_LT(*PCIDeviceKeySource().key_of(first), *PCIDeviceKeySource().key_of(second));
}

/** @brief Tests that a device with no PCI address is declined. */
TEST(Small_PCIDeviceKeySource, ADeviceWithNoAddressIsDeclined)
{
    EXPECT_FALSE(PCIDeviceKeySource().key_of(DeviceIdentity{}).has_value());
}
