/**************************************************************************/
/* numa_mapper.cpp                                                        */
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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <hwloc.h>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Implementation of the NUMA mapper.
         * 
         * Uses the hwloc library to map PCI devices to their closest NUMA nodes.
         */
        void NUMAMapper::map_distances(std::vector<HardwareNode>& nodes) 
        {
            SR_LOG_INFO("Initializing hardware topology mapping via hwloc...");

            hwloc_topology_t topology;
            
            /** @brief Step 1: initialize and load the topology. */
            if (hwloc_topology_init(&topology) < 0) 
            {
                SR_LOG_ERROR("Failed to initialize hwloc topology.");
                return;
            }
            hwloc_topology_load(topology);
            hwloc_initialized = true;

            for (auto& node : nodes) 
            {
                /** 
                 * @brief for gpu/xpu devices, we find the closest numa node using the pci location.
                 * for cpu devices, we usually assign the default numa node.
                 */
                if (node.is_gpu) 
                {
                    /** @brief Find the PCI device in the hwloc topology. */
                    hwloc_obj_t pci_device = hwloc_get_pcidev_by_busid(topology, 
                                                                    node.pci_address.domain, 
                                                                    node.pci_address.bus, 
                                                                    node.pci_address.device, 
                                                                    node.pci_address.function);

                    if (pci_device) 
                    {
                        /** @brief Find the NUMA node that is an ancestor of the PCI device. */
                        hwloc_obj_t ancestor = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NUMANODE, pci_device);
                        
                        if (ancestor) 
                        {
                            node.numa_node_id = ancestor->os_index;
                            SR_LOG_INFO("Device [{}] (PCI {:02x}:{:02x}.{:x}) is hard-bound to NUMA node {}", 
                                        node.name, node.pci_address.bus, node.pci_address.device, 
                                        node.pci_address.function, node.numa_node_id);
                        } 
                        else
                        {
                            /** @brief fallback to node 0 if no direct numa ancestor is found. */
                            node.numa_node_id = 0; 
                            SR_LOG_WARN("No direct NUMA ancestor for [{}], falling back to node 0", node.name);
                        }
                    }
                    else 
                    {
                        SR_LOG_WARN("Could not find PCI device in hwloc topology for [{}].", node.name);
                        node.numa_node_id = 0;
                    }
                } 
                else 
                {
                    /** @brief default numa node for cpu devices. */
                    node.numa_node_id = 0; 
                }
            }

            /** @brief Step 2: cleanup the topology object. */
            hwloc_topology_destroy(topology);
        }

        /**
         * @brief Returns the latency cost between nodes based on ACPI SLIT standards.
         * 
         * @param numa_node_a First NUMA node ID.
         * @param numa_node_b Second NUMA node ID.
         * @return int The cost (10 for local, 21 for remote).
         */
        int NUMAMapper::get_distance(int numa_node_a, int numa_node_b) const 
        {
            // TODO: load actual slit table from system acpi for precise latency.
            if (numa_node_a == numa_node_b) return 10; 
            return 21; 
        }
        
    } // namespace Topology
} // namespace SushiRuntime