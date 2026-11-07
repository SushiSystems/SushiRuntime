/**************************************************************************/
/* test_steal_order.cpp                                                  */
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
 * @file test_steal_order.cpp
 * @brief Tests build_steal_order(): node-local victims first, the node of each worker, one order per machine.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/steal_order.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/topology_view.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Builds a topology of four cores on NUMA nodes 0, 1, 0, 1, 10 to itself and 21 across. */
    Internal::Topology::TopologyView two_node_machine()
    {
        return Internal::Topology::TopologyView(
            Internal::Topology::InMemoryTopologyProvider({}, std::vector<int>{0, 1, 0, 1}),
            std::make_shared<Internal::Topology::MatrixDistanceModel>(
                std::vector<std::vector<int>>{{10, 21}, {21, 10}}));
    }
} // namespace

/** @brief Tests that a pinned worker takes the node of its core. */
TEST(Small_StealOrder, APinnedWorkerTakesTheNodeOfItsCore)
{
    const StealOrder order = build_steal_order(two_node_machine(), 4, PinPolicy::Pin, 0);

    EXPECT_EQ(order.worker_numa, (std::vector<int>{0, 1, 0, 1}));
}

/** @brief Tests that a worker on a core the topology does not list sits on node 0. */
TEST(Small_StealOrder, AnUnlistedCoreSitsOnNodeZero)
{
    const StealOrder order = build_steal_order(two_node_machine(), 2, PinPolicy::Pin, 3);

    EXPECT_EQ(order.worker_numa, (std::vector<int>{1, 0}));
}

/** @brief Tests that the node-local victims come first and their count is the local tier. */
TEST(Small_StealOrder, NodeLocalVictimsComeFirst)
{
    const StealOrder order = build_steal_order(two_node_machine(), 4, PinPolicy::Pin, 0);

    ASSERT_EQ(order.victim_order[0].size(), 3u);
    EXPECT_EQ(order.local_tier_len[0], 1);
    EXPECT_EQ(order.victim_order[0][0], 2);
}

/** @brief Tests that a floating pool is all on node 0, so every victim is local. */
TEST(Small_StealOrder, AFloatingPoolIsAllLocal)
{
    const StealOrder order = build_steal_order(two_node_machine(), 4, PinPolicy::Float, 0);

    EXPECT_EQ(order.local_tier_len[1], 3);
}

/** @brief Tests that one machine always gets one order. */
TEST(Small_StealOrder, OneMachineGetsOneOrder)
{
    const StealOrder first = build_steal_order(two_node_machine(), 4, PinPolicy::Pin, 0);
    const StealOrder second = build_steal_order(two_node_machine(), 4, PinPolicy::Pin, 0);

    EXPECT_EQ(first.victim_order, second.victim_order);
}
