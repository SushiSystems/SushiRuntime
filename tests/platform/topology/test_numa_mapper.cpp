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
 * @file test_numa_mapper.cpp
 * @brief Tests NUMAMapper: the local distance and the nodes it maps on this machine.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that a node is 10 from itself, the ACPI local distance. */
TEST(Unit_NUMAMapper, ALocalNodeIsTenAway)
{
    const NUMAMapper mapper;
    EXPECT_EQ(mapper.distance(0, 0), 10);
    EXPECT_EQ(mapper.distance(3, 3), 10);
}

/** @brief Tests that mapping leaves every discovered device on a node, and an empty list alone. */
TEST(Unit_NUMAMapper, MappingPlacesEveryDevice)
{
    NUMAMapper mapper;
    std::vector<HardwareNode> none;
    mapper.map_distances(none);
    EXPECT_TRUE(none.empty());

    HwlocTopologyProvider provider;
    std::vector<HardwareNode> nodes = provider.enumerate().value();
    mapper.map_distances(nodes);
    for (const HardwareNode& node : nodes)
        EXPECT_GE(node.numa_node_id, 0) << node.name;
}
