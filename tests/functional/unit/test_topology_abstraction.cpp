/****************************************************************/
/* test_topology_abstraction.cpp                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_topology_abstraction.cpp
 * @brief Tests the injectable topology provider and NUMA distance model on single-node hardware.
 * @see tests/functional/unit/README.md
 * @author Mustafa Garip
 */

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

/**
 * @brief Verifies that a hand-built MatrixDistanceModel yields exact, deterministic distances.
 *
 * Creates a synthetic 2-node distance matrix and asserts local distances are 10 while remote distances match the provided configuration.
 */
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

/**
 * @brief Verifies that the MatrixDistanceModel correctly supports asymmetric multi-hop fabric
 *        distances.
 *
 * Constructs a 3-node matrix with non-uniform interconnect costs and verifies correct distance retrievals.
 */
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

/**
 * @brief Verifies that RuntimeContext correctly utilizes an injected distance model over concrete
 *        hwloc.
 *
 * Injects a synthetic distance model into the RuntimeContext and ensures its reported NUMA distances match the injected values.
 */
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

/**
 * @brief Verifies that the RemappingTopologyProvider distributes real devices across a synthetic
 *        NUMA layout.
 *
 * Wraps a real hwloc provider with a remapper and ensures all discovered devices are assigned to NUMA nodes within the synthetic range.
 */
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

/**
 * @brief Verifies end-to-end functionality of synthetic NUMA nodes providing usable queues.
 *
 * Injects a remapping provider and tests that queues obtained for synthetic NUMA IDs can successfully submit host tasks.
 */
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
