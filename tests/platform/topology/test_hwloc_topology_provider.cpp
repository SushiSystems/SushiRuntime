/**************************************************************************/
/* test_hwloc_topology_provider.cpp                                       */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_hwloc_topology_provider.cpp
 * @brief Tests HwlocTopologyProvider and HwlocDistanceModel against the machine the test runs on.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Describes the first node of @p nodes that sits on no node of a model of @p node_count nodes, or returns an empty text. */
    std::string first_device_off_the_map(const std::vector<HardwareNode>& nodes, int node_count)
    {
        for (const HardwareNode& node : nodes)
        {
            if (node.numa_node_id < 0 || node.numa_node_id >= node_count)
                return "device [" + node.name + "] sits on node " + std::to_string(node.numa_node_id);
        }
        return {};
    }

    /** @brief Describes the first entry of @p core_to_node outside [0, @p node_count), or returns an empty text. */
    std::string first_core_off_the_map(const std::vector<int>& core_to_node, int node_count)
    {
        for (std::size_t core = 0; core < core_to_node.size(); ++core)
        {
            if (core_to_node[core] < 0 || core_to_node[core] >= node_count)
                return "core " + std::to_string(core) + " maps to node " + std::to_string(core_to_node[core]);
        }
        return {};
    }

    /** @brief Describes the first pair of nodes where a node is farther from itself than from another, or returns an empty text. */
    std::string first_remote_cheaper_than_local(const HwlocDistanceModel& distances)
    {
        for (int node = 0; node < distances.node_count(); ++node)
        {
            for (int other = 0; other < distances.node_count(); ++other)
            {
                if (distances.distance(node, node) > distances.distance(node, other))
                    return "node " + std::to_string(node) + " is nearer to node " + std::to_string(other);
            }
        }
        return {};
    }
} // namespace

/** @brief Tests that every device found sits on a node the distance model knows. */
TEST(Medium_HwlocTopologyProvider, EveryDeviceSitsOnAKnownNode)
{
    HwlocTopologyProvider provider;
    const HwlocDistanceModel distances;

    EXPECT_EQ(first_device_off_the_map(provider.enumerate().value(), distances.node_count()), "");
}

/** @brief Tests that the core map names a node the distance model knows for every core. */
TEST(Medium_HwlocTopologyProvider, TheCoreMapNamesKnownNodes)
{
    const HwlocTopologyProvider provider;
    const HwlocDistanceModel distances;
    ASSERT_GE(distances.node_count(), 1);

    EXPECT_EQ(first_core_off_the_map(provider.cpu_core_to_numa(), distances.node_count()), "");
}

/** @brief Tests that no node is farther from itself than from another node. */
TEST(Medium_HwlocTopologyProvider, TheDistancesMakeLocalNoFartherThanRemote)
{
    const HwlocDistanceModel distances;
    ASSERT_GE(distances.node_count(), 1);

    EXPECT_EQ(first_remote_cheaper_than_local(distances), "");
}
