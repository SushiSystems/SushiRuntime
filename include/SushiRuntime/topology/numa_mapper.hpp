/****************************************************************/
/* numa_mapper.hpp                                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
    namespace Topology
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