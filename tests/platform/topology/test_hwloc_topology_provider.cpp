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
 * @file test_hwloc_topology_provider.cpp
 * @brief Tests HwlocTopologyProvider and HwlocDistanceModel against the machine the test runs on.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that every device found sits on a node the distance model knows. */
TEST(Unit_HwlocTopologyProvider, EveryDeviceSitsOnAKnownNode)
{
    HwlocTopologyProvider provider;
    const HwlocDistanceModel distances;
    const std::vector<HardwareNode> nodes = provider.enumerate().value();
    for (const HardwareNode& node : nodes)
    {
        EXPECT_GE(node.numa_node_id, 0) << node.name;
        EXPECT_LT(node.numa_node_id, distances.node_count()) << node.name;
    }
}

/** @brief Tests that the core map names a node for every core, and the model is local-cheapest. */
TEST(Unit_HwlocTopologyProvider, TheCoreMapAndTheDistancesAgree)
{
    const HwlocTopologyProvider provider;
    const HwlocDistanceModel distances;
    ASSERT_GE(distances.node_count(), 1);
    for (const int node : provider.cpu_core_to_numa())
    {
        EXPECT_GE(node, 0);
        EXPECT_LT(node, distances.node_count());
    }
    for (int node = 0; node < distances.node_count(); ++node)
        for (int other = 0; other < distances.node_count(); ++other)
            EXPECT_LE(distances.distance(node, node), distances.distance(node, other));
}
