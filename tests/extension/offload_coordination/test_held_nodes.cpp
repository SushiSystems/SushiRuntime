/**************************************************************************/
/* test_held_nodes.cpp                                                    */
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
 * @file test_held_nodes.cpp
 * @brief Tests HeldNodes: the first offer, the hold, and each way a node leaves.
 * @author Mustafa Garip
 * @see tests/extension/offload_coordination/README.md
 */

#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Returns a fixed point of the steady clock plus @p offset. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }
} // namespace

/** @brief Tests that the first offer is kept until forget(), whatever later offers say. */
TEST(Small_HeldNodes, FirstOfferIsKeptUntilForgotten)
{
    HeldNodes held;
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();

    EXPECT_EQ(held.first_offer(*node, at(milliseconds(0))),
              at(milliseconds(0)));
    EXPECT_EQ(held.first_offer(*node, at(milliseconds(50))),
              at(milliseconds(0)));

    held.forget(*node);
    EXPECT_EQ(held.first_offer(*node, at(milliseconds(50))),
              at(milliseconds(50)));
}

/** @brief Tests that take_expired() returns only the waits that ended, and earliest() follows. */
TEST(Small_HeldNodes, TakeExpiredReturnsOnlyTheEndedWaits)
{
    HeldNodes held;
    const sushi_ptr<Internal::Graph::Node> early = make_sushi<Internal::Graph::Node>();
    const sushi_ptr<Internal::Graph::Node> late = make_sushi<Internal::Graph::Node>();
    EXPECT_EQ(held.earliest(), std::chrono::steady_clock::time_point::max());

    ASSERT_TRUE(held.hold(*late, at(milliseconds(200)), held.releases()) == HoldStatus::Held);
    ASSERT_TRUE(held.hold(*early, at(milliseconds(100)), held.releases()) == HoldStatus::Held);
    EXPECT_EQ(held.size(), 2u);
    EXPECT_EQ(held.earliest(), at(milliseconds(100)));

    std::vector<Internal::Graph::Node*> out;
    held.take_expired(at(milliseconds(100)), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], early.get());
    EXPECT_EQ(held.earliest(), at(milliseconds(200)));

    out.clear();
    held.take_all(out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], late.get());
    EXPECT_EQ(held.size(), 0u);
    EXPECT_EQ(held.earliest(), std::chrono::steady_clock::time_point::max());
}

/** @brief Tests that close() refuses a hold and drain() returns what was held. */
TEST(Small_HeldNodes, CloseRefusesAHoldAndDrainEmptiesTheTable)
{
    HeldNodes held;
    const sushi_ptr<Internal::Graph::Node> kept = make_sushi<Internal::Graph::Node>();
    const sushi_ptr<Internal::Graph::Node> refused = make_sushi<Internal::Graph::Node>();
    ASSERT_TRUE(held.hold(*kept, at(milliseconds(100)), held.releases()) == HoldStatus::Held);
    const auto first = held.first_offer(*kept, at(milliseconds(0)));

    held.close();
    EXPECT_EQ(held.hold(*refused, at(milliseconds(100)), held.releases()), HoldStatus::Closed);

    std::vector<Internal::Graph::Node*> out;
    held.drain(out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], kept.get());
    EXPECT_EQ(held.size(), 0u);
    EXPECT_NE(held.first_offer(*kept, at(milliseconds(30))), first)
        << "drain() forgets every first offer";
}

/** @brief Tests that a hold decided before a take_all() is refused as stale and keeps no node. */
TEST(Small_HeldNodes, HoldAfterAReleaseIsStale)
{
    HeldNodes held;
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    const std::uint64_t seen = held.releases();

    std::vector<Internal::Graph::Node*> out;
    held.take_all(out);
    EXPECT_EQ(held.releases(), seen + 1);
    EXPECT_EQ(held.hold(*node, at(milliseconds(100)), seen), HoldStatus::Stale);
    EXPECT_EQ(held.size(), 0u);
    EXPECT_EQ(held.hold(*node, at(milliseconds(100)), held.releases()), HoldStatus::Held);
}
