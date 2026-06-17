/**************************************************************************/
/* test_topology_abstraction.cpp                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Validates the injectable topology abstraction (ITopologyProvider /
// INumaDistanceModel): multi-NUMA logic exercised deterministically on
// single-node hardware, which the real hwloc path cannot do on a 1-socket box.

#include <memory>
#include <gtest/gtest.h>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

// A hand-built 2-node matrix yields exact, deterministic distances.
TEST(Unit_TopologyAbstractionTest, MatrixDistanceModelObeysAcpiConvention)
{
    MatrixDistanceModel model{{{10, 21},
                               {21, 10}}};

    EXPECT_EQ(model.node_count(), 2);
    EXPECT_EQ(model.distance(0, 0), 10); // local
    EXPECT_EQ(model.distance(1, 1), 10); // local
    EXPECT_EQ(model.distance(0, 1), 21); // remote
    EXPECT_EQ(model.distance(1, 0), 21); // symmetric remote
}

// Real interconnects are not uniform; the model returns whatever we program in.
TEST(Unit_TopologyAbstractionTest, MatrixDistanceModelSupportsAsymmetricFabric)
{
    // 3-node layout: 0<->1 cheap, 0<->2 expensive (2 hops).
    MatrixDistanceModel model{{{10, 16, 32},
                               {16, 10, 16},
                               {32, 16, 10}}};

    EXPECT_EQ(model.distance(0, 1), 16);
    EXPECT_EQ(model.distance(0, 2), 32);
    EXPECT_GT(model.distance(0, 2), model.distance(0, 1));
}

// RuntimeContext reports distances from the injected model, not concrete hwloc.
TEST(Unit_TopologyAbstractionTest, InjectedDistanceModelDrivesRuntimeContext)
{
    auto provider = std::make_shared<HwlocTopologyProvider>();
    auto distance = std::make_shared<MatrixDistanceModel>(
        std::vector<std::vector<int>>{{10, 21},
                                      {21, 10}});

    RuntimeContext ctx({}, provider, distance);

    EXPECT_EQ(ctx.numa_distance(0, 0), 10);
    EXPECT_EQ(ctx.numa_distance(0, 1), 21);
}

// The decorator keeps the real devices but spreads them over a synthetic layout;
// every device must land in range [0, N).
TEST(Unit_TopologyAbstractionTest, RemappingProviderSplitsRealDevicesAcrossFakeNodes)
{
    constexpr int FAKE_NODES = 4;

    auto real = std::make_shared<HwlocTopologyProvider>();
    RemappingTopologyProvider remapper(real, FAKE_NODES);

    HardwareFilterConfig config;
    config.include_cpu = true;
    config.include_gpu = true;

    auto nodes = remapper.enumerate(config);
    ASSERT_FALSE(nodes.empty()) << "No devices discovered to remap.";

    for (const auto& n : nodes)
    {
        EXPECT_GE(n.numa_node_id, 0);
        EXPECT_LT(n.numa_node_id, FAKE_NODES)
            << "Device [" << n.name << "] remapped outside synthetic node range.";
    }
}

// End-to-end: synthetic NUMA nodes yield usable, working queues on 1-socket HW.
TEST(Unit_TopologyAbstractionTest, RuntimeContextRoutesQueuesViaSyntheticNuma)
{
    constexpr int FAKE_NODES = 2;

    auto provider = std::make_shared<RemappingTopologyProvider>(
        std::make_shared<HwlocTopologyProvider>(), FAKE_NODES);
    auto distance = std::make_shared<MatrixDistanceModel>(
        std::vector<std::vector<int>>{{10, 21},
                                      {21, 10}});

    RuntimeContext ctx({}, provider, distance);

    for (int numa = 0; numa < FAKE_NODES; ++numa)
    {
        EXPECT_NO_THROW({
            sycl::queue& q = ctx.get_numa_local_queue(numa);
            auto ev = q.submit([](sycl::handler& h) { SushiRuntime::detail::enqueue_host_task(h, [] {}); });
            ev.wait();
        }) << "Synthetic NUMA node " << numa << " produced no working queue.";
    }
}
