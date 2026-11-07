/**************************************************************************/
/* test_numa_mapper.cpp                                                   */
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
 * @file test_numa_mapper.cpp
 * @brief Tests NUMAMapper: the local distance and the nodes it maps on this machine.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that a node is 10 from itself, the ACPI local distance. */
TEST(Small_NUMAMapper, ALocalNodeIsTenAway)
{
    const NUMAMapper mapper;
    EXPECT_EQ(mapper.distance(0, 0), 10);
    EXPECT_EQ(mapper.distance(3, 3), 10);
}

/** @brief Tests that a remote node is farther than the ACPI local distance of 10. */
TEST(Small_NUMAMapper, ARemoteNodeIsFartherThanALocalOne)
{
    const NUMAMapper mapper;
    EXPECT_GT(mapper.distance(0, 1), 10);
}

namespace
{
    /** @brief Describes the first node of @p nodes left on no NUMA node, or returns an empty text. */
    std::string first_device_left_unplaced(const std::vector<HardwareNode>& nodes)
    {
        for (const HardwareNode& node : nodes)
        {
            if (node.numa_node_id < 0)
                return "device [" + node.name + "] was left on node " + std::to_string(node.numa_node_id);
        }
        return {};
    }
} // namespace

/** @brief Tests that mapping an empty list leaves it empty. */
TEST(Medium_NUMAMapper, MappingAnEmptyListLeavesItAlone)
{
    NUMAMapper mapper;
    std::vector<HardwareNode> none;

    mapper.map_distances(none);

    EXPECT_TRUE(none.empty());
}

/** @brief Tests that mapping leaves every discovered device on a node. */
TEST(Medium_NUMAMapper, MappingPlacesEveryDevice)
{
    NUMAMapper mapper;
    HwlocTopologyProvider provider;
    std::vector<HardwareNode> nodes = provider.enumerate().value();

    mapper.map_distances(nodes);

    EXPECT_EQ(first_device_left_unplaced(nodes), "");
}
