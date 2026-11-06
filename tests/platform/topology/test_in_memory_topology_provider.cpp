/**************************************************************************/
/* test_in_memory_topology_provider.cpp                                   */
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
 * @file test_in_memory_topology_provider.cpp
 * @brief Tests InMemoryTopologyProvider: the nodes, core map and error it was built with are returned.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Builds a hardware node with the logical id, NUMA node and name given. */
    HardwareNode make_node(std::size_t logical_id, int numa_node_id, const char* name)
    {
        HardwareNode node;
        node.logical_id = logical_id;
        node.numa_node_id = numa_node_id;
        node.name = name;
        return node;
    }
} // namespace

/** @brief Tests that enumerate() returns the nodes the provider was built with. */
TEST(Small_InMemoryTopologyProvider, ReturnsTheNodesItWasGiven)
{
    InMemoryTopologyProvider provider(std::vector<HardwareNode>{make_node(0, 0, "cpu0"),
                                                                make_node(1, 1, "gpu1")});

    const Result<std::vector<HardwareNode>> result = provider.enumerate();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0].name, "cpu0");
    EXPECT_EQ(result.value()[0].numa_node_id, 0);
    EXPECT_EQ(result.value()[1].name, "gpu1");
    EXPECT_EQ(result.value()[1].numa_node_id, 1);
}

/** @brief Tests that cpu_core_to_numa() returns the core map the provider was built with. */
TEST(Small_InMemoryTopologyProvider, ReturnsTheCoreMapItWasGiven)
{
    const std::vector<int> core_map{0, 0, 1, 1};
    InMemoryTopologyProvider provider(std::vector<HardwareNode>{}, core_map);

    EXPECT_EQ(provider.cpu_core_to_numa(), core_map);
}

/** @brief Tests that the core map is empty when the provider is built without one. */
TEST(Small_InMemoryTopologyProvider, CoreMapIsEmptyWhenNoneIsGiven)
{
    InMemoryTopologyProvider provider(std::vector<HardwareNode>{make_node(0, 0, "cpu0")});

    EXPECT_TRUE(provider.cpu_core_to_numa().empty());
}

/** @brief Tests that a provider built by failing() returns the error it was given. */
TEST(Small_InMemoryTopologyProvider, FailingFormReturnsItsError)
{
    InMemoryTopologyProvider provider =
        InMemoryTopologyProvider::failing(Error{ErrorCode::Unsupported, "no devices"});

    const Result<std::vector<HardwareNode>> result = provider.enumerate();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Unsupported);
    EXPECT_EQ(result.error().message, "no devices");
}
