/**************************************************************************/
/* hwloc_topology_provider.cpp                                            */
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
#include <hwloc/distances.h>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /** @brief Default provider: real SYCL discovery + hwloc NUMA mapping. */
        std::vector<HardwareNode> HwlocTopologyProvider::enumerate(const HardwareFilterConfig& config)
        {
            HardwareDiscovery discovery;
            std::vector<HardwareNode> nodes = discovery.probe_all_devices(config);

            NUMAMapper mapper;
            mapper.map_distances(nodes);

            return nodes;
        }

        /** @brief Maps each logical CPU (PU os_index) to its NUMA node via hwloc. */
        std::vector<int> HwlocTopologyProvider::cpu_core_to_numa() const
        {
            std::vector<int> map;

            hwloc_topology_t topology;
            if (hwloc_topology_init(&topology) < 0)
            {
                SR_LOG_WARN("cpu_core_to_numa: topology init failed, returning empty map.");
                return map;
            }
            hwloc_topology_load(topology);

            const int npus = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);
            if (npus > 0)
            {
                // Size the map by the highest OS cpu index so result[i] indexes by
                // the same logical core id that thread pinning uses.
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
                    hwloc_obj_t numa = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NUMANODE, pu);
                    map[pu->os_index] = numa ? static_cast<int>(numa->os_index) : 0;
                }
            }

            hwloc_topology_destroy(topology);
            return map;
        }

        /**
         * @brief Loads and caches the NUMA distance matrix once, up front.
         *
         * This is the single place that touches the hwloc SLIT table, so the
         * (expensive) topology load happens exactly once per model instance.
         */
        HwlocDistanceModel::HwlocDistanceModel()
        {
            hwloc_topology_t topology;
            if (hwloc_topology_init(&topology) < 0)
            {
                SR_LOG_WARN("HwlocDistanceModel: topology init failed, using ACPI fallback (10/21).");
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
                SR_LOG_INFO("HwlocDistanceModel: cached {}x{} SLIT distance matrix.", dim_, dim_);
            }
            else
            {
                SR_LOG_INFO("HwlocDistanceModel: no SLIT table exposed, using ACPI fallback (10/21).");
            }

            hwloc_topology_destroy(topology);
        }

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

        int HwlocDistanceModel::node_count() const
        {
            return node_count_;
        }

    } // namespace Topology
} // namespace SushiRuntime
