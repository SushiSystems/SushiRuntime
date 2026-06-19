/**************************************************************************/
/* numa_mapper.cpp                                                        */
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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <thread>
#include <hwloc.h>
#include <hwloc/distances.h>
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
            // hwloc 2.x does not include PCI/IO devices by default.
            // KEEP_ALL is required for hwloc_get_pcidev_by_busid() to find the GPU.
            hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
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
                            char pci_str[24];
                            std::snprintf(pci_str, sizeof(pci_str), "%02x:%02x.%x",
                                          static_cast<unsigned>(node.pci_address.bus),
                                          static_cast<unsigned>(node.pci_address.device),
                                          static_cast<unsigned>(node.pci_address.function));
                            SR_LOG_INFO("Device [{}] (PCI {}) is hard-bound to NUMA node {}",
                                        node.name, pci_str, node.numa_node_id);
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
                    /** @brief Find the actual physical NUMA location for CPU nodes. */
                    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
                    hwloc_get_cpubind(topology, cpuset, HWLOC_CPUBIND_PROCESS);
                    
                    hwloc_obj_t numa_obj = hwloc_get_next_obj_covering_cpuset_by_type(topology, cpuset, HWLOC_OBJ_NUMANODE, NULL);
                    if (numa_obj) 
                    {
                        node.numa_node_id = numa_obj->os_index;
                        SR_LOG_INFO("CPU Device [{}] is bound to NUMA node {}.", node.name, node.numa_node_id);
                    } else 
                    {
                        node.numa_node_id = 0; 
                        SR_LOG_WARN("Exact NUMA assignment for CPU could not be made, Default (0) is being used.");
                    }
                    
                    hwloc_bitmap_free(cpuset);
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
            if (numa_node_a == numa_node_b) return 10; // Local node cost is always 10 (ACPI Standard)
            
            if (hwloc_initialized) 
            {
                hwloc_topology_t topology;
                hwloc_topology_init(&topology);
                hwloc_topology_load(topology);

                // Query physical latency (distance) matrices from hardware
                struct hwloc_distances_s *distances;
                unsigned int nr = 1;
                
                // Retrieve NUMA latency table (SLIT/SRAT) reported by the operating system
                if (hwloc_distances_get_by_type(topology, HWLOC_OBJ_NUMANODE, &nr, &distances, 0, 0) == 0 && nr > 0) 
                {
                    unsigned int idx_a = -1, idx_b = -1;
                    
                    // Find the indices of the nodes in the matrix
                    for (unsigned int i = 0; i < distances->nbobjs; i++) {
                        if (distances->objs[i]->os_index == (unsigned)numa_node_a) idx_a = i;
                        if (distances->objs[i]->os_index == (unsigned)numa_node_b) idx_b = i;
                    }

                    if (idx_a != (unsigned)-1 && idx_b != (unsigned)-1) 
                    {
                        // Extract the value from the matrix
                        uint64_t hwloc_val = distances->values[idx_a * distances->nbobjs + idx_b];
                        hwloc_distances_release(topology, distances);
                        hwloc_topology_destroy(topology);
                        return static_cast<int>(hwloc_val);
                    }
                    hwloc_distances_release(topology, distances);
                }
                hwloc_topology_destroy(topology);
            }
            
            // Industrial standard remote NUMA latency (Remote Hop) if hardware doesn't provide SLIT table
            return 21; 
        }
        
    } // namespace Topology
} // namespace SushiRuntime