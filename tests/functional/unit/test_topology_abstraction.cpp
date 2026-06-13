/**************************************************************************/
/* test_topology_abstraction.cpp                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_topology_abstraction.cpp
 * @brief Validates the injectable topology abstraction (ITopologyProvider /
 *        INumaDistanceModel). These tests prove that multi-NUMA logic can be
 *        exercised deterministically on single-node hardware (and under WSL),
 *        which the real hwloc path cannot do on a 1-socket desktop.
 */

#include <memory>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

/**
 * @test MatrixDistanceModelObeysAcpiConvention
 * @brief A hand-built 2-node matrix yields exact, deterministic distances —
 *        no hardware, no hwloc, no SLIT table required.
 */
TEST(TopologyAbstractionTest, MatrixDistanceModelObeysAcpiConvention)
{
    MatrixDistanceModel model{{{10, 21},
                               {21, 10}}};

    EXPECT_EQ(model.node_count(), 2);
    EXPECT_EQ(model.distance(0, 0), 10); // local
    EXPECT_EQ(model.distance(1, 1), 10); // local
    EXPECT_EQ(model.distance(0, 1), 21); // remote
    EXPECT_EQ(model.distance(1, 0), 21); // symmetric remote
}

/**
 * @test MatrixDistanceModelSupportsAsymmetricFabric
 * @brief Real interconnects (e.g. 4-node EPYC) are not uniform; the model must
 *        faithfully return whatever asymmetric costs we program in.
 */
TEST(TopologyAbstractionTest, MatrixDistanceModelSupportsAsymmetricFabric)
{
    // 3-node layout: 0<->1 cheap, 0<->2 expensive (2 hops).
    MatrixDistanceModel model{{{10, 16, 32},
                               {16, 10, 16},
                               {32, 16, 10}}};

    EXPECT_EQ(model.distance(0, 1), 16);
    EXPECT_EQ(model.distance(0, 2), 32);
    EXPECT_GT(model.distance(0, 2), model.distance(0, 1));
}

/**
 * @test InjectedDistanceModelDrivesRuntimeContext
 * @brief RuntimeContext reports distances from the *injected* model [DIP],
 *        proving the consumer no longer depends on concrete hwloc.
 */
TEST(TopologyAbstractionTest, InjectedDistanceModelDrivesRuntimeContext)
{
    auto provider = std::make_shared<HwlocTopologyProvider>();
    auto distance = std::make_shared<MatrixDistanceModel>(
        std::vector<std::vector<int>>{{10, 21},
                                      {21, 10}});

    RuntimeContext ctx({}, provider, distance);

    EXPECT_EQ(ctx.numa_distance(0, 0), 10);
    EXPECT_EQ(ctx.numa_distance(0, 1), 21);
}

/**
 * @test RemappingProviderSplitsRealDevicesAcrossFakeNodes
 * @brief The decorator keeps the real discovered devices but spreads them over
 *        a synthetic NUMA layout, so multi-node routing can be tested on a
 *        single-node machine. We assert every device lands in range [0, N).
 */
TEST(TopologyAbstractionTest, RemappingProviderSplitsRealDevicesAcrossFakeNodes)
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

/**
 * @test RuntimeContextRoutesQueuesViaSyntheticNuma
 * @brief End-to-end: a RemappingTopologyProvider + matching MatrixDistanceModel
 *        let RuntimeContext build NUMA-local queues for synthetic nodes and run
 *        real work on them — on single-node hardware. This is the scenario the
 *        bare hwloc path can never reach on a 1-socket desktop.
 */
TEST(TopologyAbstractionTest, RuntimeContextRoutesQueuesViaSyntheticNuma)
{
    constexpr int FAKE_NODES = 2;

    auto provider = std::make_shared<RemappingTopologyProvider>(
        std::make_shared<HwlocTopologyProvider>(), FAKE_NODES);
    auto distance = std::make_shared<MatrixDistanceModel>(
        std::vector<std::vector<int>>{{10, 21},
                                      {21, 10}});

    RuntimeContext ctx({}, provider, distance);

    // Every synthetic node id must yield a usable queue that runs a host_task.
    for (int numa = 0; numa < FAKE_NODES; ++numa)
    {
        EXPECT_NO_THROW({
            sycl::queue& q = ctx.get_numa_local_queue(numa);
            auto ev = q.submit([](sycl::handler& h) { h.host_task([]{}); });
            ev.wait();
        }) << "Synthetic NUMA node " << numa << " produced no working queue.";
    }
}
