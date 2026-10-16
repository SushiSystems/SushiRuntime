/**************************************************************************/
/* hwloc_topology_provider.cpp                                            */
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
 * @file hwloc_topology_provider.cpp
 * @brief Implements the default provider: SYCL discovery, hwloc NUMA mapping, cached distances.
 * @author Mustafa Garip
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <hwloc.h>
#include <hwloc/distances.h>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/numa_lookup/numa_lookup.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /** 
         * @brief Default provider: real SYCL discovery + hwloc NUMA mapping. 
         * @param config The filter configuration for device enumeration.
         * @return The enumerated hardware nodes, or Unsupported when SYCL could not list them.
         */
        Result<std::vector<HardwareNode>>
        HwlocTopologyProvider::enumerate(const DeviceFilter& config)
        {
            HardwareDiscovery discovery;
            Result<std::vector<HardwareNode>> nodes = discovery.probe_all_devices(config);
            if (!nodes)
                return nodes;

            NUMAMapper mapper;
            mapper.map_distances(*nodes);

            return nodes;
        }

        /** 
         * @brief Maps each logical CPU (PU os_index) to its NUMA node via hwloc. 
         * @return A vector mapping CPU core indices to NUMA node IDs.
         */
        std::vector<int> HwlocTopologyProvider::cpu_core_to_numa() const
        {
            std::vector<int> map;

            hwloc_topology_t topology;
            if (hwloc_topology_init(&topology) < 0)
            {
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "cpu_core_to_numa: topology init failed, returning empty map.");
                return map;
            }
            hwloc_topology_load(topology);

            const int npus = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);
            if (npus > 0)
            {
                // The map is sized by the highest OS cpu index (modules/platform/topology/README.md).
                unsigned max_os = 0;
                for (int i = 0; i < npus; ++i)
                {
                    hwloc_obj_t pu = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, i);
                    if (pu && pu->os_index > max_os) max_os = pu->os_index;
                }

                map.assign(static_cast<size_t>(max_os) + 1, 0);
                for (int i = 0; i < npus; ++i)
                {
                    hwloc_obj_t pu = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, i);
                    if (!pu) continue;
                    map[pu->os_index] = static_cast<int>(local_numa_os_index(topology, pu).value_or(0));
                }
            }

            hwloc_topology_destroy(topology);
            return map;
        }

        /**
         * @brief Constructs the HwlocDistanceModel.
         * Loads and caches the NUMA distance matrix once, up front.
         */
        HwlocDistanceModel::HwlocDistanceModel()
        {
            hwloc_topology_t topology;
            if (hwloc_topology_init(&topology) < 0)
            {
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "HwlocDistanceModel: topology init failed, using ACPI fallback (10/21).");
                return;
            }
            hwloc_topology_load(topology);

            node_count_ = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_NUMANODE);
            if (node_count_ < 1) node_count_ = 1;

            struct hwloc_distances_s* distances = nullptr;
            unsigned int nr = 1;
            if (hwloc_distances_get_by_type(topology, HWLOC_OBJ_NUMANODE, &nr, &distances, 0, 0) == 0
                && nr > 0 && distances != nullptr)
            {
                dim_ = distances->nbobjs;
                matrix_.resize(dim_ * dim_);

                for (size_t i = 0; i < dim_; ++i)
                    index_of_[static_cast<int>(distances->objs[i]->os_index)] = i;

                for (size_t i = 0; i < dim_ * dim_; ++i)
                    matrix_[i] = static_cast<int>(distances->values[i]);

                hwloc_distances_release(topology, distances);
                SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "HwlocDistanceModel: cached {}x{} SLIT distance matrix.", dim_, dim_);
            }
            else
            {
                SR_LOG_INFO(K_TOPOLOGY_LOG_CATEGORY, "HwlocDistanceModel: no SLIT table exposed, using ACPI fallback (10/21).");
            }

            hwloc_topology_destroy(topology);
        }

        /**
         * @brief Calculates the NUMA distance between two nodes.
         * Retrieves the distance from the cached SLIT table or falls back to ACPI defaults.
         * @param numa_a The ID of the first NUMA node.
         * @param numa_b The ID of the second NUMA node.
         * @return The distance cost between the two nodes.
         */
        int HwlocDistanceModel::distance(int numa_a, int numa_b) const
        {
            if (numa_a == numa_b) return 10; // ACPI: local hop is always 10.

            if (!matrix_.empty())
            {
                auto ia = index_of_.find(numa_a);
                auto ib = index_of_.find(numa_b);
                if (ia != index_of_.end() && ib != index_of_.end())
                    return matrix_[ia->second * dim_ + ib->second];
            }

            return 21; // ACPI fallback: standard single remote hop.
        }

        /**
         * @brief Gets the total number of NUMA nodes in the system.
         * @return The number of NUMA nodes.
         */
        int HwlocDistanceModel::node_count() const
        {
            return node_count_;
        }

    } // namespace Topology
} // namespace SushiRuntime
