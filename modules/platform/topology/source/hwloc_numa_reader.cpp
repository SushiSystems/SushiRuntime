/**************************************************************************/
/* hwloc_numa_reader.cpp                                                  */
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
 * @file hwloc_numa_reader.cpp
 * @brief Defines HwlocNUMAReader.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SushiRuntime/topology/hwloc_numa_reader.hpp>

#include <cstdio>
#include <hwloc.h>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/numa_lookup/numa_lookup.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        struct HwlocNUMAReader::Loaded
        {
            /** @brief Loads the topology; topology stays null when hwloc refuses. */
            Loaded() noexcept
            {
                if (hwloc_topology_init(&topology) < 0)
                {
                    topology = nullptr;
                    return;
                }
                hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_INCLUDE_DISALLOWED);
                hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
                if (hwloc_topology_load(topology) < 0)
                {
                    hwloc_topology_destroy(topology);
                    topology = nullptr;
                }
            }

            /** @brief Destroys the topology, if one was loaded. */
            ~Loaded()
            {
                if (topology != nullptr)
                    hwloc_topology_destroy(topology);
            }

            Loaded(const Loaded&) = delete;
            Loaded& operator=(const Loaded&) = delete;

            /// @brief The loaded topology, or null.
            hwloc_topology_t topology = nullptr;
        };

        HwlocNUMAReader::HwlocNUMAReader() : loaded_(std::make_unique<Loaded>())
        {
        }

        HwlocNUMAReader::~HwlocNUMAReader() = default;

        bool HwlocNUMAReader::loaded() const noexcept
        {
            return loaded_->topology != nullptr;
        }

        std::optional<int> HwlocNUMAReader::cpu_numa_node() const
        {
            const hwloc_topology_t topology = loaded_->topology;
            if (topology == nullptr)
                return std::nullopt;
            const std::optional<unsigned> local_node =
                local_numa_os_index(topology, hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, 0));
            if (!local_node.has_value())
                return std::nullopt;
            return static_cast<int>(*local_node);
        }

        std::string HwlocNUMAReader::gpu_pci_address(const std::string& device_name) const
        {
            const hwloc_topology_t topology = loaded_->topology;
            if (topology == nullptr)
                return {};
            hwloc_obj_t osdev = nullptr;
            while ((osdev = hwloc_get_next_osdev(topology, osdev)) != nullptr)
            {
                if (osdev->attr->osdev.type != HWLOC_OBJ_OSDEV_GPU)
                    continue;
                if (osdev->name == nullptr || device_name.find(osdev->name) == std::string::npos)
                    continue;

                // The BDF address sits on the PCI object above the OS device.
                hwloc_obj_t io = osdev->parent;
                while (io && io->type != HWLOC_OBJ_PCI_DEVICE)
                    io = io->parent;
                if (io == nullptr)
                    return {};
                char text[32];
                std::snprintf(text, sizeof(text), "%04x:%02x:%02x.%x", io->attr->pcidev.domain,
                              io->attr->pcidev.bus, io->attr->pcidev.dev, io->attr->pcidev.func);
                SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY, "Device {} found via hwloc osdev scan: {}",
                             device_name, text);
                return text;
            }
            return {};
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
