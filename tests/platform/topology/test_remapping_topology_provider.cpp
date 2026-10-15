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
 * @file test_remapping_topology_provider.cpp
 * @brief Tests RemappingTopologyProvider: real devices spread across synthetic NUMA nodes.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace TopologyAbstractionTests
{
    /**
     * @brief Verifies that the RemappingTopologyProvider distributes real devices across a synthetic
     *        NUMA layout.
     *
     * Wraps a real hwloc provider with a remapper and ensures all discovered devices are assigned to NUMA nodes within the synthetic range.
     */
    TEST(Small_TopologyAbstractionTest, RemappingProviderSplitsRealDevicesAcrossFakeNodes)
    {
        constexpr int FAKE_NODES = 4;

        auto real = std::make_shared<HwlocTopologyProvider>();
        RemappingTopologyProvider remapper(real, FAKE_NODES);

        DeviceFilter config;
        config.include_cpu = true;
        config.include_gpu = true;

        auto nodes = remapper.enumerate(config).value();
        ASSERT_FALSE(nodes.empty()) << "No devices discovered to remap.";

        for (const auto& n : nodes)
        {
            EXPECT_GE(n.numa_node_id, 0);
            EXPECT_LT(n.numa_node_id, FAKE_NODES)
                << "Device [" << n.name << "] remapped outside synthetic node range.";
        }
    }
} // namespace TopologyAbstractionTests
