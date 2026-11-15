/**************************************************************************/
/* device_key_source.hpp                                                  */
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
 * @file device_key_source.hpp
 * @brief Declares DeviceIdentity and DeviceKeySource, the port one source of a DeviceKey implements.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief What discovery knows of one device when it names it.
         *
         * A plain value: no SYCL type is in it, so a fake topology builds one.
         */
        struct DeviceIdentity
        {
            /// @brief Backend that reports the device.
            DeviceBackend backend = DeviceBackend::Host;

            /// @brief Vendor string of the device; empty when the driver gives none.
            std::string vendor{};

            /// @brief Name of the device.
            std::string name{};

            /// @brief The 16-byte UUID the driver reports; empty when it reports none.
            std::optional<HardwareID> uuid = std::nullopt;

            /// @brief The PCI address, from the driver or from hwloc; empty when neither has one.
            std::optional<PCIDomainAddress> pci_address = std::nullopt;

            /// @brief Total global memory size in bytes.
            std::size_t global_mem_size = 0;

            /// @brief Total number of compute units.
            std::uint32_t max_compute_units = 0;
        };

        /**
         * @brief Produces the DeviceKey one kind of hardware id gives a device.
         * @see modules/platform/topology/README.md
         */
        class DeviceKeySource
        {
            public:
                virtual ~DeviceKeySource() = default;

                /**
                 * @brief Returns the key this source gives the device, or nothing when the
                 *        device has no id of this source's kind.
                 *
                 * The ordinal of the key is 0; DeviceKeyAssigner sets it.
                 */
                [[nodiscard]] virtual std::optional<DeviceKey>
                key_of(const DeviceIdentity& identity) const = 0;

            protected:
                DeviceKeySource() = default;
                DeviceKeySource(const DeviceKeySource&) = default;
                DeviceKeySource& operator=(const DeviceKeySource&) = default;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
