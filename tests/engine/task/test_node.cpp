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
 * @file test_node.cpp
 * @brief Tests Node: successors inline and spilled, readiness on completion, placement and offload state.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that a successor becomes ready when its last predecessor completes. */
TEST(Unit_Node, ASuccessorIsReadyAfterItsLastPredecessor)
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
TEST(Unit_Node, SuccessorsPastTheInlineCapacityAreKept)
{
    constexpr std::size_t K_SUCCESSORS = Node::K_MAX_INLINE_SUCCESSORS + 5;
    const sushi_ptr<Node> source = make_sushi<Node>();
    std::vector<sushi_ptr<Node>> targets;
    for (std::size_t index = 0; index < K_SUCCESSORS; ++index)
    {
        targets.push_back(make_sushi<Node>());
        source->add_successor(targets.back().get());
    }
    std::vector<Node*> seen;
    source->for_each_successor([&seen](Node* successor) { seen.push_back(successor); });
    EXPECT_EQ(seen.size(), K_SUCCESSORS);
    for (const sushi_ptr<Node>& target : targets)
        EXPECT_EQ(std::count(seen.begin(), seen.end(), target.get()), 1) << "a successor was lost or doubled";
}

/** @brief Tests arm_in_degree(): the intra and the boundary counts add up. */
TEST(Unit_Node, ArmingAddsTheIntraAndBoundaryCounts)
{
    Node node;
    node.region.intra_in_degree = 2;
    node.region.boundary_in_degree.store(3);
    node.arm_in_degree();
    EXPECT_EQ(node.in_degree.load(), 5);
}

/** @brief Tests that placement is unstated until place_on(), and the offload state is made once. */
TEST(Unit_Node, PlacementAndOffloadStateStartEmpty)
{
    Node node;
    EXPECT_FALSE(node.stated_placement().has_value());
    node.place_on(3);
    ASSERT_TRUE(node.stated_placement().has_value());
    EXPECT_EQ(*node.stated_placement(), 3u);

    EXPECT_EQ(node.offload_state(), nullptr);
    OffloadState& state = node.offload();
    EXPECT_EQ(&node.offload(), &state);
    node.set_live_io(std::make_shared<std::vector<void*>>(1, nullptr), nullptr);
    EXPECT_EQ(node.offload_state()->live_reads->size(), 1u);
    EXPECT_TRUE(node.is_graph_node());
    EXPECT_FALSE(node.is_splittable());
}
