/**************************************************************************/
/* test_hw_discovery.cpp                                                  */
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
 * @file test_hw_discovery.cpp
 * @brief Tests what HardwareDiscovery returns for the filters a caller can state.
 * @author Mustafa Garip
 */

#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>

using namespace SushiRuntime::Topology;

namespace
{
    /** @brief Returns the filter that admits no device class. */
    HardwareFilterConfig filter_admitting_nothing()
    {
        HardwareFilterConfig config;
        config.include_cpu = false;
        config.include_gpu = false;
        config.include_accelerator = false;
        return config;
    }
} // namespace

// An empty result is a value at this tier, not a failure; see README.md.
TEST(Unit_HardwareDiscovery, AFilterThatAdmitsNoClassYieldsNoDeviceAndDoesNotThrow)
{
    HardwareDiscovery discovery;
    std::vector<HardwareNode> probed;
    ASSERT_NO_THROW(probed = discovery.probe_all_devices(filter_admitting_nothing()));
    EXPECT_TRUE(probed.empty());

    HwlocTopologyProvider provider;
    std::vector<HardwareNode> enumerated;
    ASSERT_NO_THROW(enumerated = provider.enumerate(filter_admitting_nothing()));
    EXPECT_TRUE(enumerated.empty());
}

TEST(Unit_HardwareDiscovery, EveryNodeTheGpuOnlyFilterAdmitsIsAGpu)
{
    HardwareFilterConfig config;
    config.include_cpu = false;
    config.include_accelerator = false;

    HardwareDiscovery discovery;
    for (const HardwareNode& node : discovery.probe_all_devices(config))
        EXPECT_TRUE(node.sycl_dev.is_gpu()) << "device [" << node.name << "] is not a GPU";
}
