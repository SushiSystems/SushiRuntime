/**************************************************************************/
/* pci_device_key_source.cpp                                              */
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
 * @file pci_device_key_source.cpp
 * @brief Defines PCIDeviceKeySource.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/pci_device_key_source.hpp>

#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        std::optional<DeviceKey> PCIDeviceKeySource::key_of(const DeviceIdentity& identity) const
        {
            if (!identity.pci_address.has_value())
                return std::nullopt;

            const PCIDomainAddress& address = *identity.pci_address;
            DeviceKey key;
            key.backend = identity.backend;
            key.id_kind = HardwareIDKind::PCIAddress;
            key.hardware_id[0] = static_cast<std::uint8_t>(address.domain >> 24);
            key.hardware_id[1] = static_cast<std::uint8_t>(address.domain >> 16);
            key.hardware_id[2] = static_cast<std::uint8_t>(address.domain >> 8);
            key.hardware_id[3] = static_cast<std::uint8_t>(address.domain);
            key.hardware_id[4] = static_cast<std::uint8_t>(address.bus);
            key.hardware_id[5] = static_cast<std::uint8_t>(address.device);
            key.hardware_id[6] = static_cast<std::uint8_t>(address.function);
            return key;
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
