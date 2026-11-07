/**************************************************************************/
/* hw_discovery.hpp                                                       */
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
 * @file hw_discovery.hpp
 * @brief Declares HardwareDiscovery and the hardware node and filter types.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/topology/device_enumerator.hpp>
#include <SushiRuntime/topology/numa_reader.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief PCI Bus-Device-Function (BDF) address.
         * 
         * Used to map SYCL devices to NUMA nodes using hwloc.
         */
        struct PCIDomainAddress 
        {
            std::uint32_t domain = 0;   /**< PCI domain */
            std::uint32_t bus = 0;      /**< PCI bus */
            std::uint32_t device = 0;   /**< PCI device */
            std::uint32_t function = 0; /**< PCI function */
        };

        /**
         * @brief Reads a PCI address written as "domain:bus:device.function", all four in hex.
         * @param text The string a driver or hwloc reported.
         * @param device_name Names the device in the DEBUG line written when @p text does not parse.
         * @return The address, or nothing when @p text holds no such address.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::optional<PCIDomainAddress>
        parse_pci_address(const std::string& text, const std::string& device_name);

        /**
         * @brief Represents a hardware unit like CPU or GPU.
         */
        struct HardwareNode
        {
            std::size_t logical_id = 0; /**< Logical ID within SushiRuntime. */
            int numa_node_id = -1;      /**< ID of the NUMA node from hwloc; -1 until mapped. */
            std::size_t device_id = 0;  /**< Id the enumerator gave the device. */
            DeviceKind kind = DeviceKind::Other; /**< The class the device reports. */
            std::string name{};         /**< Name of the device. */
            bool is_gpu = false;        /**< True if the device is a GPU. */

            /** @brief Holds the PCI address; empty when no source reported one. */
            std::optional<PCIDomainAddress> pci_address = std::nullopt;

            std::size_t global_mem_size = 0;     /**< Total global memory size in bytes. */
            std::uint32_t max_compute_units = 0; /**< Total number of compute units. */
        };

        /**
         * @brief Filters the devices an enumerator lists and maps each to its NUMA node.
         *
         * Used once at RuntimeContext construction to build the device list. At most one
         * CPU is kept per NUMA node, and at most one GPU or accelerator per resolved PCI
         * address, the first enumerated; a device with no PCI address is always kept.
         */
        class SUSHIRUNTIME_API HardwareDiscovery
        {
            public:
                /**
                 * @brief Builds a discovery over an enumeration and a NUMA reader.
                 * @param enumerator Lists the devices; must outlive the discovery.
                 * @param reader Places the CPU and finds a GPU's PCI address; must outlive it.
                 */
                HardwareDiscovery(DeviceEnumerator& enumerator, NUMAReader& reader) noexcept;

                /**
                 * @brief Lists the devices and returns those the filter admits as hardware nodes.
                 * @param config Device-class filter; defaults to including all classes.
                 * @return The devices kept, with logical IDs and NUMA assignments, or
                 *         Unsupported when the enumerator could not list the devices.
                 */
                [[nodiscard]] Result<std::vector<HardwareNode>>
                probe_all_devices(const DeviceFilter& config = {});

            private:
                /// @brief Lists the devices.
                DeviceEnumerator& enumerator_;
                /// @brief Places the CPU and finds a GPU's PCI address.
                NUMAReader& reader_;
        };

    } // namespace Topology
} // namespace SushiRuntime