/**************************************************************************/
/* test_remapping_topology_provider.cpp                                   */
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
 * @file test_remapping_topology_provider.cpp
 * @brief Tests RemappingTopologyProvider: the devices of an inner provider spread across synthetic NUMA nodes.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief One inner provider of @p devices devices and a number of synthetic nodes, and the node each device must get. */
    struct RemapRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief How many devices the inner provider reports, all on node 0. */
        std::size_t devices;
        /** @brief The number of synthetic nodes handed to the provider. */
        int fake_nodes;
        /** @brief The synthetic node of each device, in order. */
        std::vector<int> expected;
    };

    /** @brief Builds the nodes of an inner provider: @p devices devices, all on NUMA node 0. */
    std::vector<HardwareNode> devices_on_node_zero(std::size_t devices)
    {
        std::vector<HardwareNode> nodes(devices);
        for (std::size_t i = 0; i < devices; ++i)
        {
            nodes[i].logical_id = i;
            nodes[i].numa_node_id = 0;
        }
        return nodes;
    }
} // namespace

class Small_RemappingTopologyProvider : public ::testing::TestWithParam<RemapRow>
{
};

/** @brief Tests that the devices of the inner provider are spread round-robin across the synthetic nodes. */
TEST_P(Small_RemappingTopologyProvider, SplitsDevicesAcrossFakeNodes)
{
    const RemapRow& row = GetParam();
    auto inner = std::make_shared<InMemoryTopologyProvider>(devices_on_node_zero(row.devices));
    RemappingTopologyProvider remapper(inner, row.fake_nodes);

    std::vector<int> assigned;
    for (const HardwareNode& node : remapper.enumerate().value())
        assigned.push_back(node.numa_node_id);

    EXPECT_EQ(assigned, row.expected);
}

INSTANTIATE_TEST_SUITE_P(Layouts, Small_RemappingTopologyProvider,
                         ::testing::Values(RemapRow{"SixDevicesOverFourNodes", 6, 4, {0, 1, 2, 3, 0, 1}},
                                           RemapRow{"ThreeDevicesOverTwoNodes", 3, 2, {0, 1, 0}},
                                           RemapRow{"ANodeCountBelowOneBecomesOne", 3, 0, {0, 0, 0}}),
                         [](const ::testing::TestParamInfo<RemapRow>& info) { return std::string(info.param.name); });
