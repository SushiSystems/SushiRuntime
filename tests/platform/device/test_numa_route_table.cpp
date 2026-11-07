/**************************************************************************/
/* test_numa_route_table.cpp                                              */
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
 * @file test_numa_route_table.cpp
 * @brief Tests NUMARouteTable: the device each NUMA node resolves to, the placement of a bad id and a named device.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/device/numa_route_table.hpp>

using SushiRuntime::Internal::Device::NUMARouteTable;

namespace
{
    /** @brief One NUMA id asked of a table whose devices sit on nodes 2, 0 and 2, and the device it resolves to. */
    struct LookupRow
    {
        int numa_node;       /**< The node asked for. */
        std::size_t device;  /**< The device it resolves to. */
    };

    /** @brief One NUMA id a device may report, and the node the table places it on. */
    struct PlacementRow
    {
        int reported; /**< The id the device reports. */
        int placed;   /**< The node the table places it on. */
    };
} // namespace

class Small_NUMARouteTableLookup : public ::testing::TestWithParam<LookupRow>
{
};

/** @brief Tests that a node with a device resolves to its lowest-indexed one, and any other id to device 0. */
TEST_P(Small_NUMARouteTableLookup, ResolvesTheFirstDeviceOfTheNodeOrDeviceZero)
{
    const NUMARouteTable table(std::vector<int>{2, 0, 2});

    EXPECT_EQ(table.device_for(GetParam().numa_node), GetParam().device);
}

INSTANTIATE_TEST_SUITE_P(Nodes, Small_NUMARouteTableLookup,
                         ::testing::Values(LookupRow{0, 1}, LookupRow{2, 0}, LookupRow{1, 0}, LookupRow{3, 0},
                                           LookupRow{999, 0}, LookupRow{-5, 0}));

class Small_NUMARouteTablePlacement : public ::testing::TestWithParam<PlacementRow>
{
};

/** @brief Tests that an id outside [0, K_MAX_NUMA_NODES) is placed on node 0 and any other id stays. */
TEST_P(Small_NUMARouteTablePlacement, PlacesAnInvalidIDOnNodeZero)
{
    EXPECT_EQ(NUMARouteTable::placed_node(GetParam().reported), GetParam().placed);
}

INSTANTIATE_TEST_SUITE_P(Ids, Small_NUMARouteTablePlacement,
                         ::testing::Values(PlacementRow{1, 1}, PlacementRow{0, 0}, PlacementRow{-1, 0},
                                           PlacementRow{4096, 0}));

/** @brief Tests that devices which report an invalid id serve node 0 with the device that sits there. */
TEST(Small_NUMARouteTable, DevicesOnAnInvalidIDServeNodeZero)
{
    const NUMARouteTable table(std::vector<int>{1, -1, 4096});

    EXPECT_EQ(table.device_for(0), 1u);
    EXPECT_EQ(table.device_for(1), 0u);
}

/** @brief Tests that a named device wins over the rotation of the fallback node. */
TEST(Small_NUMARouteTable, ANamedDeviceWinsOverTheRotation)
{
    const NUMARouteTable table(std::vector<int>{0, 2, 2});

    EXPECT_EQ(table.device_or_next(std::optional<std::size_t>{2}, 0), 2u);
    EXPECT_EQ(table.device_or_next(std::optional<std::size_t>{0}, 2), 0u);
}

/** @brief Tests that the table names the nodes below its highest one that hold no device. */
TEST(Small_NUMARouteTable, NamesTheNodesThatHoldNoDevice)
{
    const NUMARouteTable table(std::vector<int>{3, 0});

    EXPECT_EQ(table.node_count(), 4u);
    EXPECT_EQ(table.populated_count(), 2u);
    EXPECT_EQ(table.empty_nodes(), "1, 2");
}
