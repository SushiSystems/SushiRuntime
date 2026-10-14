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
 * @file test_topology_provider.cpp
 * @brief Tests the TopologyProvider and NUMADistanceModel contracts over a fake of each.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Reports two devices on two nodes and keeps the default core map. */
    class TwoNodeProvider final : public TopologyProvider
    {
        public:
            [[nodiscard]] Result<std::vector<HardwareNode>> enumerate(const DeviceFilter&) override
            {
                std::vector<HardwareNode> nodes(2);
                nodes[0].logical_id = 0;
                nodes[0].numa_node_id = 0;
                nodes[1].logical_id = 1;
                nodes[1].numa_node_id = 1;
                return nodes;
            }
    };

    /** @brief Answers 10 on the diagonal and 20 off it, over two nodes. */
    class TwoNodeDistances final : public NUMADistanceModel
    {
        public:
            int distance(int numa_a, int numa_b) const override
            {
                return numa_a == numa_b ? 10 : 20;
            }

            int node_count() const override
            {
                return 2;
            }
    };
}

/** @brief Tests that a provider is called through the interface, and the core map defaults empty. */
TEST(Unit_TopologyProvider, AProviderAnswersThroughTheInterface)
{
    TwoNodeProvider provider;
    TopologyProvider& seam = provider;
    const std::vector<HardwareNode> nodes = seam.enumerate().value();
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[1].numa_node_id, 1);
    EXPECT_TRUE(seam.cpu_core_to_numa().empty());
}

/** @brief Tests a distance model through the interface: local is the cheapest. */
TEST(Unit_TopologyProvider, ADistanceModelAnswersThroughTheInterface)
{
    const TwoNodeDistances distances;
    const NUMADistanceModel& seam = distances;
    EXPECT_EQ(seam.node_count(), 2);
    EXPECT_LT(seam.distance(0, 0), seam.distance(0, 1));
}
