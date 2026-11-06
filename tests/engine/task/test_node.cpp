/**************************************************************************/
/* test_node.cpp                                                          */
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
 * @file test_node.cpp
 * @brief Tests Node: successors inline and spilled, readiness on completion, placement and offload state.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Registers @p count new nodes as successors of @p source and returns them. */
    std::vector<sushi_ptr<Node>> add_successors(Node& source, std::size_t count)
    {
        std::vector<sushi_ptr<Node>> targets;
        for (std::size_t index = 0; index < count; ++index)
        {
            targets.push_back(make_sushi<Node>());
            source.add_successor(targets.back().get());
        }
        return targets;
    }

    /** @brief Returns the successors @p source visits, sorted by address. */
    std::vector<Node*> visited_successors(const Node& source)
    {
        std::vector<Node*> seen;
        source.for_each_successor([&seen](Node* successor) { seen.push_back(successor); });
        std::sort(seen.begin(), seen.end());
        return seen;
    }

    /** @brief Returns the addresses of @p nodes, sorted. */
    std::vector<Node*> addresses_of(const std::vector<sushi_ptr<Node>>& nodes)
    {
        std::vector<Node*> addresses;
        for (const sushi_ptr<Node>& node : nodes)
            addresses.push_back(node.get());
        std::sort(addresses.begin(), addresses.end());
        return addresses;
    }
} // namespace

/** @brief Tests that a successor becomes ready when its last predecessor completes. */
TEST(Small_Node, ASuccessorIsReadyAfterItsLastPredecessor)
{
    const sushi_ptr<Node> first = make_sushi<Node>();
    const sushi_ptr<Node> second = make_sushi<Node>();
    const sushi_ptr<Node> join = make_sushi<Node>();
    first->add_successor(join.get());
    second->add_successor(join.get());
    join->region.intra_in_degree = 2;
    join->arm_in_degree();

    Internal::Scheduler::ReadySink ready;
    first->on_complete(ready);
    EXPECT_TRUE(ready.empty()) << "the join became ready with one predecessor left";
    second->on_complete(ready);
    ASSERT_EQ(ready.size(), 1u);
    EXPECT_EQ(ready.front(), join.get());
}

/** @brief Tests that successors past the inline capacity are kept and each visited once. */
TEST(Small_Node, SuccessorsPastTheInlineCapacityAreKept)
{
    const sushi_ptr<Node> source = make_sushi<Node>();
    const std::vector<sushi_ptr<Node>> targets = add_successors(*source, Node::K_MAX_INLINE_SUCCESSORS + 5);

    EXPECT_EQ(visited_successors(*source), addresses_of(targets)) << "a successor was lost or doubled";
}

/** @brief Tests arm_in_degree(): the intra and the boundary counts add up. */
TEST(Small_Node, ArmingAddsTheIntraAndBoundaryCounts)
{
    Node node;
    node.region.intra_in_degree = 2;
    node.region.boundary_in_degree.store(3);
    node.arm_in_degree();
    EXPECT_EQ(node.in_degree.load(), 5);
}

/** @brief Tests that placement is unstated until place_on(), and then holds the index last stated. */
TEST(Small_Node, PlacementIsUnstatedUntilPlaceOn)
{
    Node node;
    EXPECT_FALSE(node.stated_placement().has_value());

    node.place_on(0);
    EXPECT_EQ(node.stated_placement(), std::optional<std::size_t>(0));

    node.place_on(3);
    EXPECT_EQ(node.stated_placement(), std::optional<std::size_t>(3));
}

/** @brief Tests that the offload state is absent until asked for and then made once. */
TEST(Small_Node, TheOffloadStateIsMadeOnceOnDemand)
{
    Node node;
    EXPECT_EQ(node.offload_state(), nullptr);

    OffloadState& state = node.offload();

    EXPECT_EQ(&node.offload(), &state);
    EXPECT_EQ(node.offload_state(), &state);
}

/** @brief Tests that set_live_io() stores the lists the next dispatch reads. */
TEST(Small_Node, SetLiveIoStoresTheListsOfTheNextDispatch)
{
    Node node;
    const std::shared_ptr<std::vector<void*>> reads = std::make_shared<std::vector<void*>>(1, nullptr);

    node.set_live_io(reads, nullptr);

    EXPECT_EQ(node.offload_state()->live_reads, reads);
    EXPECT_EQ(node.offload_state()->live_writes, nullptr);
}
