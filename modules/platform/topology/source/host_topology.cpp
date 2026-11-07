/**************************************************************************/
/* host_topology.cpp                                                      */
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
 * @file host_topology.cpp
 * @brief Defines make_host_topology(), the machine a host runtime stands for.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/host_topology.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief The SLIT distance of a node to itself. */
            constexpr int K_LOCAL_DISTANCE = 10;
        } // namespace

        HostTopology make_host_topology(std::size_t device_count)
        {
            SR_VERIFY(device_count > 0, "make_host_topology: pass a device count of at least 1.");
            HostTopology topology;
            topology.provider = std::make_unique<InMemoryTopologyProvider>(
                std::vector<HardwareNode>{}, std::vector<int>{0});
            topology.distance_model = std::make_shared<MatrixDistanceModel>(
                std::vector<std::vector<int>>{{K_LOCAL_DISTANCE}});
            topology.devices.resize(device_count);
            for (std::size_t index = 0; index < device_count; ++index)
            {
                topology.devices[index].index = index;
                topology.devices[index].name = "host device " + std::to_string(index);
                topology.devices[index].kind = DeviceKind::CPU;
            }
            return topology;
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
