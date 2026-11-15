/**************************************************************************/
/* test_physical_card_selector.cpp                                        */
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
 * @file test_physical_card_selector.cpp
 * @brief Tests select_physical_cards.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/physical_card_selector.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Returns a device on a backend, with a name, a UUID and a PCI bus, any of them absent. */
    DeviceIdentity device(DeviceBackend backend, const char* name,
                          std::optional<std::uint8_t> uuid = std::nullopt,
                          std::optional<std::uint32_t> bus = std::nullopt)
    {
        DeviceIdentity identity;
        identity.backend = backend;
        identity.name = name;
        if (uuid.has_value())
            identity.uuid = HardwareID{*uuid};
        if (bus.has_value())
            identity.pci_address = PCIDomainAddress{0, *bus, 0, 0};
        return identity;
    }
} // namespace

/** @brief Tests that devices with no id are all kept. */
TEST(Small_PhysicalCardSelector, DevicesWithNoIDAreAllKept)
{
    const std::vector<DeviceIdentity> listed = {device(DeviceBackend::OpenCL, "a"),
                                                device(DeviceBackend::OpenCL, "a")};

    EXPECT_EQ(select_physical_cards(listed), (std::vector<std::size_t>{0, 1}));
}

/** @brief Tests that two devices at one PCI address are one card, and the native one is kept. */
TEST(Small_PhysicalCardSelector, OneCardAtOnePCIAddressKeepsTheNativeDevice)
{
    const std::vector<DeviceIdentity> listed = {
        device(DeviceBackend::OpenCL, "card", std::nullopt, 1u),
        device(DeviceBackend::CUDA, "card", std::nullopt, 1u)};

    EXPECT_EQ(select_physical_cards(listed), (std::vector<std::size_t>{1}));
}

/** @brief Tests that two devices with one UUID and no PCI address are one card. */
TEST(Small_PhysicalCardSelector, OneCardWithOneUUIDKeepsTheNativeDevice)
{
    const std::vector<DeviceIdentity> listed = {device(DeviceBackend::OpenCL, "card", 5),
                                                device(DeviceBackend::HIP, "card", 5)};

    EXPECT_EQ(select_physical_cards(listed), (std::vector<std::size_t>{1}));
}

/** @brief Tests that a UUID on one device and a PCI address on another join through a third. */
TEST(Small_PhysicalCardSelector, ACardIsAChainOfSharedIDs)
{
    const std::vector<DeviceIdentity> listed = {
        device(DeviceBackend::CUDA, "card", 9), device(DeviceBackend::OpenCL, "card", 9, 4u),
        device(DeviceBackend::OpenCL, "card", std::nullopt, 4u)};

    EXPECT_EQ(select_physical_cards(listed), (std::vector<std::size_t>{0}));
}

/** @brief Tests that the kept device does not depend on the order the devices are listed in. */
TEST(Small_PhysicalCardSelector, TheKeptDeviceDoesNotDependOnTheListOrder)
{
    const DeviceIdentity cuda = device(DeviceBackend::CUDA, "card", 3);
    const DeviceIdentity opencl = device(DeviceBackend::OpenCL, "card", 3);

    EXPECT_EQ(select_physical_cards({cuda, opencl}), (std::vector<std::size_t>{0}));
    EXPECT_EQ(select_physical_cards({opencl, cuda}), (std::vector<std::size_t>{1}));
}

/** @brief Tests that devices at different addresses are different cards. */
TEST(Small_PhysicalCardSelector, DevicesAtDifferentAddressesAreDifferentCards)
{
    const std::vector<DeviceIdentity> listed = {
        device(DeviceBackend::CUDA, "card", std::nullopt, 1u),
        device(DeviceBackend::CUDA, "card", std::nullopt, 2u)};

    EXPECT_EQ(select_physical_cards(listed), (std::vector<std::size_t>{0, 1}));
}
