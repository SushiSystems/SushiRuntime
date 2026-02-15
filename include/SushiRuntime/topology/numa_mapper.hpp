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

#include "hw_discovery.hpp"
#include <vector>

namespace SushiRuntime
{
    namespace Topology
    {
        /**
         * @brief maps memory access distances (numa distance) between hardware units.
         */
        class NUMAMapper 
        {
            public:
                NUMAMapper() = default;

                /**
                 * @brief identifies the closest numa node for each device in the list.
                 * @param nodes the list of hardware nodes to map.
                 */
                void map_distances(std::vector<HardwareNode>& nodes);

                /**
                 * @brief returns the latency cost between two numa nodes.
                 * @param numa_node_a first numa node id.
                 * @param numa_node_b second numa node id.
                 * @return int the distance or latency value.
                 */
                int get_distance(int numa_node_a, int numa_node_b) const;

            private:
                /** @brief flag to indicate if hwloc is ready. */
                bool hwloc_initialized = false;
        };
        
    } // namespace Topology
} // namespace SushiRuntime