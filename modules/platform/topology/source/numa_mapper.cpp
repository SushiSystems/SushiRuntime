/**************************************************************************/
/* numa_mapper.cpp                                                        */
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
 * @file numa_mapper.cpp
 * @brief Maps each discovered device to its NUMA node through hwloc.
 * @author Mustafa Garip
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <optional>
#include <thread>
#include <hwloc.h>
#include <hwloc/distances.h>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include "numa_lookup.hpp"

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Implementation of the NUMA mapper.
         * 
         * Uses the hwloc library to map PCI devices to their closest NUMA nodes.
         * @param nodes The vector of hardware nodes to map.
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
            // KEEP_ALL lets hwloc_get_pcidev_by_busid() find the GPU (modules/platform/topology/README.md).
            hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
            hwloc_topology_load(topology);
            hwloc_initialized = true;

            const int numa_node_count = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_NUMANODE);

            for (auto& node : nodes)
            {
                // A device with a PCI address, or a GPU without one, is placed by the bus.
                if (node.pci_address.has_value() || node.is_gpu)
                {
                    // A device without a PCI address has nothing to look up on the bus.
                    if (!node.pci_address.has_value())
                    {
                        node.numa_node_id = 0;
                        if (numa_node_count > 1)
                        {
                            SR_LOG_WARN("No PCI address for [{}]; bus lookup skipped, placed on NUMA node 0 of {}, which may not be its nearest.",
                                        node.name, numa_node_count);
                        }
                        else
                        {
                            SR_LOG_INFO("No PCI address for [{}] (expected on WSL2, where hwloc sees no PCI devices); bus lookup skipped, placed on NUMA node 0, the only node.",
                                        node.name);
                        }
                        continue;
                    }
                    const PCIDomainAddress& pci_address = *node.pci_address;

                    /** @brief Find the PCI device in the hwloc topology. */
                    hwloc_obj_t pci_device = hwloc_get_pcidev_by_busid(topology,
                                                                    pci_address.domain,
                                                                    pci_address.bus,
                                                                    pci_address.device,
                                                                    pci_address.function);

                    if (pci_device) 
                    {
                        const std::optional<unsigned> local_node =
                            local_numa_os_index(topology, pci_device);
                        
                        if (local_node.has_value()) 
                        {
                            node.numa_node_id = static_cast<int>(*local_node);
                            char pci_str[24];
                            std::snprintf(pci_str, sizeof(pci_str), "%02x:%02x.%x",
                                          static_cast<unsigned>(pci_address.bus),
                                          static_cast<unsigned>(pci_address.device),
                                          static_cast<unsigned>(pci_address.function));
                            SR_LOG_INFO("Device [{}] (PCI {}) is hard-bound to NUMA node {}",
                                        node.name, pci_str, node.numa_node_id);
                        } 
                        else
                        {
                            node.numa_node_id = 0;
                            if (numa_node_count > 1)
                            {
                                SR_LOG_WARN("hwloc reports no local NUMA node for [{}]; placed on NUMA node 0 of {}, which may not be its nearest.",
                                            node.name, numa_node_count);
                            }
                            else
                            {
                                SR_LOG_INFO("hwloc reports no local NUMA node for [{}]; placed on NUMA node 0, the only node.",
                                            node.name);
                            }
                        }
                    }
                    else 
                    {
                        node.numa_node_id = 0;
                        if (numa_node_count > 1)
                        {
                            SR_LOG_WARN("hwloc lists no PCI device for [{}]; placed on NUMA node 0 of {}, which may not be its nearest.",
                                        node.name, numa_node_count);
                        }
                        else
                        {
                            SR_LOG_INFO("hwloc lists no PCI device for [{}]; placed on NUMA node 0, the only node.",
                                        node.name);
                        }
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
        int NUMAMapper::distance(int numa_node_a, int numa_node_b) const 
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
            
            // ACPI default remote distance (local is 10), used when no SLIT table is provided.
            return 21; 
        }
        
    } // namespace Topology
} // namespace SushiRuntime