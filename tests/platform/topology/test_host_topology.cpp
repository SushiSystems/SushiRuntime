/**************************************************************************/
/* test_host_topology.cpp                                                 */
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
 * @file test_host_topology.cpp
 * @brief Tests make_host_topology(): one NUMA node, local distances and CPU host devices.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/topology/host_topology.hpp>

using SushiRuntime::DeviceKind;
using SushiRuntime::Internal::Topology::HostTopology;
using SushiRuntime::Internal::Topology::make_host_topology;

/** @brief Tests that the host machine maps its core to NUMA node 0 and reads no hardware node. */
TEST(Small_HostTopology, MapsItsCoreToNodeZeroAndHasNoHardwareNode)
{
    HostTopology topology = make_host_topology(1);

    EXPECT_EQ(topology.provider->cpu_core_to_numa(), std::vector<int>{0});
    EXPECT_TRUE(topology.provider->enumerate().value().empty());
}

/** @brief Tests that the host machine has one NUMA node at the local distance of 10. */
TEST(Small_HostTopology, HasOneNodeAtTheLocalDistance)
{
    HostTopology topology = make_host_topology(1);

    EXPECT_EQ(topology.distance_model->node_count(), 1);
    EXPECT_EQ(topology.distance_model->distance(0, 0), 10);
}

/** @brief Tests that each host device is a CPU named for its index. */
TEST(Small_HostTopology, NamesEachDeviceForItsIndexAsACPU)
{
    HostTopology topology = make_host_topology(2);

    ASSERT_EQ(topology.devices.size(), 2u);
    EXPECT_EQ(topology.devices[1].name, std::string("host device 1"));
    EXPECT_EQ(topology.devices[1].kind, DeviceKind::CPU);
    EXPECT_EQ(topology.devices[1].index, 1u);
}
