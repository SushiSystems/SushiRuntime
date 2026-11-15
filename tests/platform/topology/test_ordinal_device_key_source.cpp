/**************************************************************************/
/* test_ordinal_device_key_source.cpp                                     */
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
 * @file test_ordinal_device_key_source.cpp
 * @brief Tests OrdinalDeviceKeySource.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/topology/ordinal_device_key_source.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that the source names a device by its backend, with no id and ordinal 0. */
TEST(Small_OrdinalDeviceKeySource, TheKeyHoldsTheBackendAndNoID)
{
    DeviceIdentity identity;
    identity.backend = DeviceBackend::HIP;

    const std::optional<DeviceKey> key = OrdinalDeviceKeySource().key_of(identity);

    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->backend, DeviceBackend::HIP);
    EXPECT_EQ(key->id_kind, HardwareIDKind::None);
    EXPECT_EQ(key->hardware_id, HardwareID{});
    EXPECT_EQ(key->ordinal, 0u);
}

/** @brief Tests that the source answers for a device that has a UUID and a PCI address too. */
TEST(Small_OrdinalDeviceKeySource, TheSourceAlwaysAnswers)
{
    DeviceIdentity identity;
    identity.uuid = HardwareID{};
    identity.pci_address = PCIDomainAddress{0, 1, 0, 0};

    EXPECT_TRUE(OrdinalDeviceKeySource().key_of(identity).has_value());
}
