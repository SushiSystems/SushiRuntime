/**************************************************************************/
/* remapping_topology_provider.cpp                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <stdexcept>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        RemappingTopologyProvider::RemappingTopologyProvider(std::shared_ptr<ITopologyProvider> inner,
                                                             int fake_node_count)
            : inner_(std::move(inner)), fake_node_count_(fake_node_count)
        {
            if (!inner_)
                throw std::invalid_argument("RemappingTopologyProvider: inner provider must not be null.");
            if (fake_node_count_ < 1)
                fake_node_count_ = 1;
        }

        std::vector<HardwareNode> RemappingTopologyProvider::enumerate(const HardwareFilterConfig& config)
        {
            std::vector<HardwareNode> nodes = inner_->enumerate(config);

            // Spread the real devices across the synthetic NUMA nodes round-robin.
            // Devices stay real (queues/kernels still work); only numa_node_id is faked.
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                int fake_numa = static_cast<int>(i % static_cast<size_t>(fake_node_count_));
                SR_LOG_DEBUG("Remapping device [{}] from NUMA {} to synthetic NUMA {}.",
                             nodes[i].name, nodes[i].numa_node_id, fake_numa);
                nodes[i].numa_node_id = fake_numa;
            }

            SR_LOG_INFO("RemappingTopologyProvider: {} devices spread across {} synthetic NUMA nodes.",
                        nodes.size(), fake_node_count_);
            return nodes;
        }

        std::vector<int> RemappingTopologyProvider::cpu_core_to_numa() const
        {
            // Overlay the same synthetic round-robin layout on the real cores, so
            // hierarchical work stealing sees multiple NUMA nodes on single-node
            // hardware. Index = logical core id, kept aligned with thread pinning.
            std::vector<int> map = inner_->cpu_core_to_numa();
            for (size_t i = 0; i < map.size(); ++i)
                map[i] = static_cast<int>(i % static_cast<size_t>(fake_node_count_));
            return map;
        }

    } // namespace Topology
} // namespace SushiRuntime
