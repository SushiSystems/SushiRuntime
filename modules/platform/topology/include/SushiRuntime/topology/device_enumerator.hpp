/**************************************************************************/
/* device_enumerator.hpp                                                  */
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
 * @file device_enumerator.hpp
 * @brief Declares DeviceEnumerator, the port HardwareDiscovery lists the devices through.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_kind.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief One device as the enumeration reports it, before any filter or NUMA mapping.
         *
         * A plain value: only a SYCL adapter maps an id to and from a SYCL device.
         */
        struct DeviceDescriptor
        {
            std::size_t id = 0;                  /**< Position of the device in the enumeration. */
            std::string name{};                  /**< Name of the device. */
            DeviceKind kind = DeviceKind::Other; /**< The class the device reports. */
            bool fp64 = false;                   /**< True when the device runs double precision. */
            std::size_t global_mem_size = 0;     /**< Total global memory size in bytes. */
            std::uint32_t max_compute_units = 0; /**< Total number of compute units. */
            /** @brief The PCI address text the driver reports; empty when it reports none. */
            std::string pci_text{};
        };

        /**
         * @brief Lists every device of the machine, in enumeration order.
         * @see modules/platform/topology/README.md
         */
        class DeviceEnumerator
        {
            public:
                virtual ~DeviceEnumerator() = default;

                /**
                 * @brief Returns every device, or an Unsupported error whose message is the
                 *        text the device runtime gave when it could not list them.
                 */
                [[nodiscard]] virtual Result<std::vector<DeviceDescriptor>> enumerate() = 0;

            protected:
                DeviceEnumerator() = default;
                DeviceEnumerator(const DeviceEnumerator&) = default;
                DeviceEnumerator& operator=(const DeviceEnumerator&) = default;
        };
    } // namespace Internal::Topology
} // namespace SushiRuntime
