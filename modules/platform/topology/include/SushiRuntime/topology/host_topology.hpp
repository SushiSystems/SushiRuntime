/**************************************************************************/
/* host_topology.hpp                                                      */
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
 * @file host_topology.hpp
 * @brief Declares HostTopology and make_host_topology(), the machine a host runtime stands for.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Holds the machine of a host runtime: one NUMA node, no hardware node, and host
         *        devices that are CPUs.
         * @see modules/platform/topology/README.md
         */
        struct HostTopology
        {
            std::unique_ptr<TopologyProvider> provider;              /**< Maps every core to node 0. */
            std::shared_ptr<const NUMADistanceModel> distance_model; /**< One node, local distance 10. */
            std::vector<DeviceDescription> devices;                  /**< "host device <i>", CPUs. */
        };

        /**
         * @brief Builds the machine a host runtime of some devices stands for; it reads no hardware.
         * @param device_count The number of host devices; at least 1.
         */
        [[nodiscard]] SUSHIRUNTIME_API HostTopology make_host_topology(std::size_t device_count);
    } // namespace Internal::Topology
} // namespace SushiRuntime
