/**************************************************************************/
/* numa_mapper.hpp                                                        */
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
 * @file numa_mapper.hpp
 * @brief Declares NUMAMapper, which maps distances between hardware units.
 * @author Mustafa Garip
 */

#pragma once

#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Maps memory access distances between hardware units.
         *
         * Analyzes hardware topology to associate devices with their closest NUMA nodes.
         */
        class SUSHIRUNTIME_API NUMAMapper 
        {
            public:
                /** @brief Default constructor. */
                NUMAMapper() = default;

                /**
                 * @brief Identifies the closest NUMA node for each device.
                 *
                 * Updates each node's NUMA assignment based on physical hwloc proximity data.
                 * @param nodes The list of hardware nodes to update in place.
                 */
                void map_distances(std::vector<HardwareNode>& nodes);

                /**
                 * @brief Retrieves the latency cost between two NUMA nodes.
                 *
                 * Queries the underlying hwloc system for the distance between the nodes.
                 * @param numa_node_a The first NUMA node index.
                 * @param numa_node_b The second NUMA node index.
                 * @return The distance value, typically 10 for local and >10 for remote access.
                 */
                int get_distance(int numa_node_a, int numa_node_b) const;

            private:
                /** @brief Flag to indicate if hwloc is ready. */
                bool hwloc_initialized = false;
        };
        
    } // namespace Topology
} // namespace SushiRuntime