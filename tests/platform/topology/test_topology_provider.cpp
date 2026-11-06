/**************************************************************************/
/* test_topology_provider.cpp                                             */
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
 * @file test_topology_provider.cpp
 * @brief Tests the TopologyProvider and NUMADistanceModel contracts over the in-memory form of each.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Builds two hardware nodes, one on each of two NUMA nodes. */
    std::vector<HardwareNode> two_nodes()
    {
        std::vector<HardwareNode> nodes(2);
        nodes[0].logical_id = 0;
        nodes[0].numa_node_id = 0;
        nodes[1].logical_id = 1;
        nodes[1].numa_node_id = 1;
        return nodes;
    }
} // namespace

/** @brief Tests that a provider is called through the interface, and the core map defaults empty. */
TEST(Small_TopologyProvider, AProviderAnswersThroughTheInterface)
{
    InMemoryTopologyProvider provider(two_nodes());
    TopologyProvider& seam = provider;

    const std::vector<HardwareNode> nodes = seam.enumerate().value();

    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[1].numa_node_id, 1);
    EXPECT_TRUE(seam.cpu_core_to_numa().empty());
}

/** @brief Tests a distance model through the interface: local is the cheapest. */
TEST(Small_TopologyProvider, ADistanceModelAnswersThroughTheInterface)
{
    const MatrixDistanceModel distances{{{10, 20}, {20, 10}}};
    const NUMADistanceModel& seam = distances;

    EXPECT_EQ(seam.node_count(), 2);
    EXPECT_LT(seam.distance(0, 0), seam.distance(0, 1));
}
