/**************************************************************************/
/* hwloc_topology_provider.hpp                                            */
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
 * @file hwloc_topology_provider.hpp
 * @brief Declares HwlocTopologyProvider and HwlocDistanceModel.
 * @author Mustafa Garip
 */

#pragma once

#include <map>
#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Hardware topology provider backed by SYCL and hwloc.
         *
         * Delegates to HardwareDiscovery for device enumeration and NUMAMapper for NUMA assignment.
         */
        class SUSHIRUNTIME_API HwlocTopologyProvider : public ITopologyProvider
        {
            public:
                /** @brief Default constructor. */
                HwlocTopologyProvider() = default;

                /**
                 * @brief Discovers and catalogs all available hardware devices.
                 *
                 * Scans the system using hwloc and SYCL to build a list of compute nodes.
                 * @param config The hardware filter configuration defining inclusion or exclusion rules.
                 * @return The list of discovered hardware nodes ready for scheduling.
                 */
                std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) override;

                /**
                 * @brief Maps CPU cores to their corresponding NUMA nodes.
                 *
                 * Extracts the physical core to NUMA node mapping from the hwloc topology.
                 * @return An array where the index represents the core ID and the value represents the NUMA node ID.
                 */
                std::vector<int> cpu_core_to_numa() const override;
        };

        /**
         * @brief NUMA distance model backed by hwloc SLIT and SRAT tables.
         *
         * The topology and its distance matrix are loaded once at construction and cached,
         * making distance lookups an O(1) operation. Falls back to the ACPI convention
         * (10 local, 21 remote) when the firmware exposes no SLIT table.
         */
        class SUSHIRUNTIME_API HwlocDistanceModel : public INumaDistanceModel
        {
            public:
                /** @brief Default constructor. */
                HwlocDistanceModel();

                /**
                 * @brief Retrieves the memory access distance between two NUMA nodes.
                 *
                 * Looks up the latency cost from the cached SLIT matrix or ACPI fallback.
                 * @param numa_a The index of the first NUMA node.
                 * @param numa_b The index of the second NUMA node.
                 * @return The relative latency cost, where 10 is local and higher values are remote.
                 */
                int distance(int numa_a, int numa_b) const override;

                /**
                 * @brief Retrieves the total number of NUMA nodes in the system.
                 *
                 * Returns the node count populated during initialization.
                 * @return The count of available NUMA nodes.
                 */
                int node_count() const override;

            private:
                /** @brief os_index -> row/col index inside @ref matrix_. */
                std::map<int, size_t> index_of_;
                /** @brief Flattened nbobjs x nbobjs SLIT matrix (empty if unavailable). */
                std::vector<int> matrix_;
                size_t dim_ = 0;
                int node_count_ = 1;
        };

    } // namespace Topology
} // namespace SushiRuntime
