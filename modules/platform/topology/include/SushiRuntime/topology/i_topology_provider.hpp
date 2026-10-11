/**************************************************************************/
/* i_topology_provider.hpp                                                */
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
 * @file i_topology_provider.hpp
 * @brief Declares ITopologyProvider and INUMADistanceModel.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Declares the interface over hardware discovery and NUMA mapping.
         * @see modules/platform/topology/README.md
         */
        class SUSHIRUNTIME_API ITopologyProvider
        {
            public:
                virtual ~ITopologyProvider() = default;

                /**
                 * @brief Discovers compute devices and assigns their NUMA ids.
                 * @param config Filter controlling which device classes are returned.
                 * @return Hardware nodes with numa_node_id resolved (>= 0) or -1 if unknown.
                 */
                virtual std::vector<HardwareNode> enumerate(const HardwareFilterConfig& config = {}) = 0;

                /**
                 * @brief Maps each logical CPU (OS cpu index) to its NUMA node; empty by default.
                 * @see modules/platform/topology/README.md
                 */
                virtual std::vector<int> cpu_core_to_numa() const { return {}; }
        };

        /**
         * @brief Interface over the NUMA distance (ACPI SLIT) matrix.
         *
         * Kept separate from device discovery so it can be independently replaced.
         * Most topology-aware scheduling logic needs only distances, not device lists.
         */
        class SUSHIRUNTIME_API INUMADistanceModel
        {
            public:
                virtual ~INUMADistanceModel() = default;

                /**
                 * @brief Latency cost between two NUMA nodes.
                 * @return 10 for local (a == b), > 10 for remote (ACPI convention).
                 */
                virtual int distance(int numa_a, int numa_b) const = 0;

                /** @brief Number of NUMA nodes the model knows about. */
                virtual int node_count() const = 0;
        };

    } // namespace Topology
} // namespace SushiRuntime
