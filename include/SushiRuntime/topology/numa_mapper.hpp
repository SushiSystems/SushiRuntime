/**************************************************************************/
/* numa_mapper.hpp                                                        */
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

#pragma once

#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief Maps memory access distances (NUMA distance) between hardware units.
         */
        class SUSHIRUNTIME_API NUMAMapper 
        {
            public:
                NUMAMapper() = default;

                /**
                 * @brief Identifies the closest NUMA node for each device in the list.
                 * @param nodes The list of hardware nodes to map.
                 */
                void map_distances(std::vector<HardwareNode>& nodes);

                /**
                 * @brief Returns the latency cost between two NUMA nodes.
                 * @param numa_node_a First NUMA node ID.
                 * @param numa_node_b Second NUMA node ID.
                 * @return int The distance or latency value.
                 */
                int get_distance(int numa_node_a, int numa_node_b) const;

            private:
                /** @brief Flag to indicate if hwloc is ready. */
                bool hwloc_initialized = false;
        };
        
    } // namespace Topology
} // namespace SushiRuntime