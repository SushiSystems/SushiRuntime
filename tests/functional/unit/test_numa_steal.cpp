/**************************************************************************/
/* test_numa_steal.cpp                                                    */
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
 * @file test_numa_steal.cpp
 * @brief Tests that node-local steal victims precede remote ones under a synthetic NUMA topology.
 * @see tests/functional/unit/README.md
 * @author Mustafa Garip
 */

#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

#include "../../common/scheduler_fixture.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::make_scheduler;
using namespace SushiRuntime::Topology;

namespace
{
    /**
     * @brief Creates a synthetic RuntimeContext split into `nodes` NUMA nodes.
     *
     * Generates a context with a symmetric distance matrix mapping core i -> i % nodes.
     *
     * @param nodes The number of synthetic NUMA nodes.
     * @return A shared pointer to the created RuntimeContext.
     */
    std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
    {
        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), nodes);

        std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
        for (int i = 0; i < nodes; ++i)
            matrix[i][i] = 10;
        auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));

        return std::make_shared<RuntimeContext>(Topology::HardwareFilterConfig{}, provider, distance);
    }
}

/**
 * @brief Tests that node-local victims precede remote ones in the steal order.
 *
 * With cores split across nodes, every worker's steal order must list all
 * same-node victims before any cross-node victim.
 */
TEST(Unit_NumaStealTest, LocalVictimsPrecedeRemote)
{
    constexpr int NODES = 2;
    auto ctx = make_synthetic_ctx(NODES);

    TaskScheduler sched = make_scheduler(*ctx); // construction only — workers are not started.

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

        // Once the first remote victim appears, no local victim may follow.
        bool seen_remote = false;
        for (int victim : order)
        {
            const bool is_local = (victim % NODES) == my_node;
            if (!is_local)
                seen_remote = true;
            else
                EXPECT_FALSE(seen_remote)
                    << "Worker " << th << ": local victim " << victim
                    << " appears after a remote victim — locality violated.";
        }
    }
}

/**
 * @brief Tests that a flat topology keeps all victims in the steal order.
 *
 * The default (real) topology collapses to one node here; the steal order
 * must still contain every other worker.
 */
TEST(Unit_NumaStealTest, FlatTopologyKeepsAllVictims)
{
    RuntimeContext ctx; // default Hwloc provider/model.
    TaskScheduler sched = make_scheduler(ctx);

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
