/**************************************************************************/
/* hwloc_topology_provider.hpp                                            */
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
         * @brief Real hardware topology provider, backed by SYCL + hwloc.
         *
         * Delegates to HardwareDiscovery (device enumeration + PCI resolution)
         * and NUMAMapper (NUMA assignment). This is the default provider used by
         * RuntimeContext when none is injected.
         */
        class SUSHIRUNTIME_API HwlocTopologyProvider : public ITopologyProvider
        {
            public:
                HwlocTopologyProvider() = default;
                std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) override;
                std::vector<int> cpu_core_to_numa() const override;
        };

        /**
         * @brief Real NUMA distance model, backed by the hwloc SLIT/SRAT tables.
         *
         * Unlike the previous NUMAMapper::get_distance(), the topology and its
         * distance matrix are loaded *once* at construction and cached, so
         * distance() is a cheap O(1) lookup instead of a full hwloc reload per
         * call. Falls back to the ACPI convention (10 local / 21 remote) when the
         * firmware exposes no SLIT table (typical under WSL / single-node desktops).
         */
        class SUSHIRUNTIME_API HwlocDistanceModel : public INumaDistanceModel
        {
            public:
                HwlocDistanceModel();

                int distance(int numa_a, int numa_b) const override;
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
