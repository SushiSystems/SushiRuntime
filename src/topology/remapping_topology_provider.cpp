/**************************************************************************/
/* remapping_topology_provider.cpp                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
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
