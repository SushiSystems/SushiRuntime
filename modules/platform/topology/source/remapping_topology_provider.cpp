/**************************************************************************/
/* remapping_topology_provider.cpp                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file remapping_topology_provider.cpp
 * @brief Remaps devices and CPU cores onto synthetic NUMA nodes.
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Constructs a new RemappingTopologyProvider.
         * Wraps an existing provider to map devices into synthetic NUMA nodes.
         * @param inner The underlying topology provider to wrap.
         * @param fake_node_count The number of synthetic NUMA nodes to simulate.
         * @pre inner is not null.
         */
        RemappingTopologyProvider::RemappingTopologyProvider(std::shared_ptr<TopologyProvider> inner,
                                                             int fake_node_count)
            : inner_(std::move(inner)), fake_node_count_(fake_node_count)
        {
            SR_VERIFY(inner_ != nullptr,
                      "RemappingTopologyProvider: the inner provider is null; pass the provider "
                      "whose devices are to be remapped.");
            if (fake_node_count_ < 1)
                fake_node_count_ = 1;
        }

        /**
         * @brief Enumerates hardware nodes and remaps their NUMA IDs.
         * Distributes discovered devices across the synthetic NUMA nodes in a round-robin fashion.
         * @param config The hardware filter configuration.
         * @return A vector of hardware nodes with synthetic NUMA node IDs.
         */
        std::vector<HardwareNode> RemappingTopologyProvider::enumerate(const HardwareFilterConfig& config)
        {
            std::vector<HardwareNode> nodes = inner_->enumerate(config);

            // Only numa_node_id is faked; the devices stay real (modules/platform/topology/README.md).
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

        /**
         * @brief Maps CPU cores to synthetic NUMA nodes.
         * Overlays a synthetic round-robin layout on the real cores.
         * @return A vector mapping CPU core indices to synthetic NUMA node IDs.
         */
        std::vector<int> RemappingTopologyProvider::cpu_core_to_numa() const
        {
            // The index is the logical core id thread pinning uses (modules/platform/topology/README.md).
            std::vector<int> map = inner_->cpu_core_to_numa();
            for (size_t i = 0; i < map.size(); ++i)
                map[i] = static_cast<int>(i % static_cast<size_t>(fake_node_count_));
            return map;
        }

    } // namespace Topology
} // namespace SushiRuntime
