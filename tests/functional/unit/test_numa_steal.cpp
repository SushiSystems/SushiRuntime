/**************************************************************************/
/* test_numa_steal.cpp                                                    */
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
 * @file test_numa_steal.cpp
 * @brief Validates NUMA-aware hierarchical work stealing on single-node
 *        hardware by injecting a synthetic topology.
 *
 * A RemappingTopologyProvider spreads the real cores across N synthetic NUMA
 * nodes (round-robin: core i -> node i % N) and a MatrixDistanceModel supplies
 * the matching distance matrix. The scheduler then builds its steal victim
 * table from this fake topology, so we can assert the locality invariant —
 * node-local victims always precede remote ones — without real multi-socket
 * hardware. The actual latency benefit is only measurable on bare metal; this
 * test guards the ordering *logic*.
 */

#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using namespace SushiRuntime::Topology;

namespace
{
    // Build a RuntimeContext whose topology is split into `nodes` synthetic NUMA
    // nodes (core i -> i % nodes), with a matching symmetric distance matrix
    // (10 local, 21 remote).
    std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
    {
        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), nodes);

        std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
        for (int i = 0; i < nodes; ++i) matrix[i][i] = 10;
        auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));

        return std::make_shared<RuntimeContext>(Topology::HardwareFilterConfig{}, provider, distance);
    }
}

/**
 * @test LocalVictimsPrecedeRemote
 * @brief With cores split across 2 synthetic nodes (node = id % 2), every
 *        worker's steal order must list all same-node victims before any
 *        cross-node victim.
 */
TEST(NumaStealTest, LocalVictimsPrecedeRemote)
{
    constexpr int NODES = 2;
    auto ctx = make_synthetic_ctx(NODES);

    TaskScheduler sched(*ctx); // construction only — workers are not started.

    const int workers = static_cast<int>(sched.get_worker_count());
    if (workers < 2)
        GTEST_SKIP() << "Need at least 2 worker threads to exercise stealing order.";

    for (int th = 0; th < workers; ++th)
    {
        const int my_node = th % NODES;
        const std::vector<int> order = sched.steal_victim_order(th);

        // The worker steals from everyone except itself.
        ASSERT_EQ(order.size(), static_cast<size_t>(workers - 1))
            << "Victim list for worker " << th << " has wrong size.";

        // Invariant: once the first remote victim appears, no local victim may
        // follow — i.e. the local tier is a strict prefix.
        bool seen_remote = false;
        for (int victim : order)
        {
            const bool is_local = (victim % NODES) == my_node;
            if (!is_local) seen_remote = true;
            else
                EXPECT_FALSE(seen_remote)
                    << "Worker " << th << ": local victim " << victim
                    << " appears after a remote victim — locality violated.";
        }
    }
}

/**
 * @test FlatTopologyKeepsAllVictims
 * @brief Default (real) topology collapses to a single node on this hardware;
 *        the steal order must still contain every other worker (NUMA-blind
 *        single tier == original behaviour, no regression).
 */
TEST(NumaStealTest, FlatTopologyKeepsAllVictims)
{
    RuntimeContext ctx; // default Hwloc provider/model.
    TaskScheduler sched(ctx);

    const int workers = static_cast<int>(sched.get_worker_count());
    if (workers < 2)
        GTEST_SKIP() << "Need at least 2 worker threads.";

    const std::vector<int> order = sched.steal_victim_order(0);
    EXPECT_EQ(order.size(), static_cast<size_t>(workers - 1));

    // No self, no duplicates, all in range.
    std::vector<bool> seen(workers, false);
    for (int v : order)
    {
        ASSERT_GE(v, 0);
        ASSERT_LT(v, workers);
        EXPECT_NE(v, 0) << "Worker 0 must not be its own steal victim.";
        EXPECT_FALSE(seen[v]) << "Duplicate victim " << v;
        seen[v] = true;
    }
}
