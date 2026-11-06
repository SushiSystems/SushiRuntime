/**************************************************************************/
/* test_node_placement.cpp                                                */
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
 * @file test_node_placement.cpp
 * @brief Tests the placement a node states and the device placement_of names for it.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;
    using SushiRuntime::Internal::Graph::placement_of;
} // namespace

/** @brief Holds memory of two devices that share one SYCL context; a test records ownership with record(). */
class Small_NodePlacementRule : public ::testing::Test
{
    protected:
        /// @brief Two devices in context 0.
        SushiRuntime::Internal::Graph::InMemoryPlacementMemory memory_{std::vector<std::size_t>{0, 0}};
};

/** @brief Holds memory of two devices that sit in two SYCL contexts; a test records ownership with record(). */
class Small_NodePlacementContexts : public ::testing::Test
{
    protected:
        /// @brief Device 0 in context 0 and device 1 in context 1.
        SushiRuntime::Internal::Graph::InMemoryPlacementMemory memory_{std::vector<std::size_t>{0, 1}};
};

/** @brief Tests that place_on stops the process on an index it cannot hold, and the earlier placement stays. A death test starts a child process. */
TEST(Medium_NodePlacementStateDeathTest, PlaceOnStopsOnAnIndexItCannotHold)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    const std::size_t too_large = Node::K_NO_PLACEMENT;
    EXPECT_DEATH(node->place_on(too_large),
                 "Node::place_on." + std::to_string(too_large) + ".: a device index must be below");
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(1));
}

/** @brief Tests that a node with no region and nothing stated runs on the default device, 0. */
TEST_F(Small_NodePlacementRule, ANodeWithNoRegionRunsOnTheDefaultWhenNothingIsStated)
{
    const sushi_ptr<Node> node = make_sushi<Node>();

    EXPECT_EQ(placement_of(*node, memory_).value(), SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
    EXPECT_EQ(SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT, 0u);
}

/** @brief Tests that a node with no region runs on the device it states. */
TEST_F(Small_NodePlacementRule, ANodeWithNoRegionRunsOnItsStatedDevice)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Tests that a node whose only region is a read runs on the owner of that region. */
TEST_F(Small_NodePlacementRule, ANodeWithOnlyAReadRegionRunsOnItsOwner)
{
    int read_cell = 0;
    memory_.record(&read_cell, 1);
    const sushi_ptr<Node> reader = make_sushi<Node>();
    reader->io_reads_.emplace_back(&read_cell);

    EXPECT_EQ(placement_of(*reader, memory_).value(), 1u);
}

/** @brief Tests that the owner of the first write outranks the owner of the first read. */
TEST_F(Small_NodePlacementRule, TheFirstWriteRegionOutranksTheFirstReadRegion)
{
    int read_cell = 0;
    int write_cell = 0;
    memory_.record(&read_cell, 0);
    memory_.record(&write_cell, 1);
    const sushi_ptr<Node> both = make_sushi<Node>();
    both->io_reads_.emplace_back(&read_cell);
    both->io_writes_.emplace_back(&write_cell);

    EXPECT_EQ(placement_of(*both, memory_).value(), 1u);
}

/** @brief Tests that an owned region places a node that states nothing. */
TEST_F(Small_NodePlacementRule, AnOwnedRegionPlacesANodeThatStatesNothing)
{
    int cell = 0;
    memory_.record(&cell, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

/** @brief Tests that a stated device in the owner's context outranks the owner. */
TEST_F(Small_NodePlacementRule, AStatedDeviceInTheOwnersContextOutranksTheOwner)
{
    int cell = 0;
    memory_.record(&cell, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);
}

/** @brief Tests that a stated device in another SYCL context than the owner's is refused, naming the owner. */
TEST_F(Small_NodePlacementContexts, AStatedDeviceInAnotherContextIsRefused)
{
    int cell = 0;
    memory_.record(&cell, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);

    const SushiRuntime::Result<std::size_t> placed = placement_of(*node, memory_);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 1 in another SYCL context"), std::string::npos)
        << placed.error().message;
}

/** @brief Tests that stating the owner itself is accepted, whatever the contexts. */
TEST_F(Small_NodePlacementContexts, StatingTheOwnerItselfIsAccepted)
{
    int cell = 0;
    memory_.record(&cell, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u) << "the owner itself is never refused";
}

/** @brief Tests that a stated index past the devices is returned as stated, for the dispatch to refuse. */
TEST_F(Small_NodePlacementContexts, AStatedIndexPastTheDevicesIsLeftToTheDispatch)
{
    int cell = 0;
    memory_.record(&cell, 1);
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(5);

    EXPECT_EQ(placement_of(*node, memory_).value(), 5u);
}

/** @brief Tests that an unowned region with nothing stated runs on device 0. */
TEST_F(Small_NodePlacementRule, AnUnownedRegionWithNothingStatedRunsOnDeviceZero)
{
    int cell = 0;
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(&cell);

    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);
}

/** @brief Tests that a stated device stands for an unowned region. */
TEST_F(Small_NodePlacementRule, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(&cell);
    node->place_on(1);

    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}
