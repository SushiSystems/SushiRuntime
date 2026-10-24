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
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;
    using SushiRuntime::Internal::Graph::placement_of;

    /** @brief Returns two queues of one context on the device SYCL selects by default. */
    std::vector<sycl::queue> two_queues_on_the_default_device()
    {
        const sycl::device device{sycl::default_selector_v};
        const sycl::context context{device};
        return {sycl::queue{context, device}, sycl::queue{context, device}};
    }

    /** @brief Returns two queues on the default device, each in a context of its own. */
    std::vector<sycl::queue> two_queues_in_two_contexts()
    {
        const sycl::device device{sycl::default_selector_v};
        return {sycl::queue{sycl::context{device}, device},
                sycl::queue{sycl::context{device}, device}};
    }
} // namespace

/** @brief Holds an allocator over two device entries; see README.md. */
class Medium_NodePlacementRule : public ::testing::Test
{
    protected:
        Medium_NodePlacementRule() : memory_(two_queues_on_the_default_device())
        {

        }

        /// @brief Allocates nothing; a test records ownership with adopt().
        SushiRuntime::Internal::Memory::USMAllocator memory_;
};

TEST(Medium_NodePlacementState, ANodeStatesNoPlacementUntilPlaceOn)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    EXPECT_FALSE(node->stated_placement().has_value());

    node->place_on(0);
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(0));

    node->place_on(3);
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(3));
}

TEST(Medium_NodePlacementStateDeathTest, PlaceOnStopsOnAnIndexItCannotHold)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    const std::size_t too_large = Node::K_NO_PLACEMENT;
    EXPECT_DEATH(node->place_on(too_large),
                 "Node::place_on." + std::to_string(too_large) + ".: a device index must be below");
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(1));
}

TEST_F(Medium_NodePlacementRule, ANodeWithNoRegionRunsOnItsStatedDeviceElseOnTheDefault)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    EXPECT_EQ(placement_of(*node, memory_).value(), SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
    EXPECT_EQ(SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT, 0u);

    node->place_on(1);
    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}

TEST_F(Medium_NodePlacementRule, TheFirstWriteRegionOutranksTheFirstReadRegion)
{
    int read_cell = 0;
    int write_cell = 0;
    memory_.adopt(&read_cell, 0, sizeof(read_cell));
    memory_.adopt(&write_cell, 1, sizeof(write_cell));

    const sushi_ptr<Node> reader = make_sushi<Node>();
    reader->io_reads_.emplace_back(&write_cell);
    EXPECT_EQ(placement_of(*reader, memory_).value(), 1u);

    const sushi_ptr<Node> both = make_sushi<Node>();
    both->io_reads_.emplace_back(&read_cell);
    both->io_writes_.emplace_back(&write_cell);
    EXPECT_EQ(placement_of(*both, memory_).value(), 1u);

    memory_.forget(&read_cell);
    memory_.forget(&write_cell);
}

TEST_F(Medium_NodePlacementRule, AnOwnedRegionPlacesANodeThatStatesNothing)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell));

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);

    memory_.forget(&cell);
}

TEST_F(Medium_NodePlacementRule, AStatedDeviceInTheOwnersContextOutranksTheOwner)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell));

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);
    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);

    memory_.forget(&cell);
}

TEST(Medium_NodePlacementContexts, AStatedDeviceInAnotherContextIsRefused)
{
    SushiRuntime::Internal::Memory::USMAllocator memory(two_queues_in_two_contexts());
    int cell = 0;
    memory.adopt(&cell, 1, sizeof(cell));

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);
    const SushiRuntime::Result<std::size_t> placed = placement_of(*node, memory);

    ASSERT_FALSE(placed);
    EXPECT_EQ(placed.error().code, SushiRuntime::ErrorCode::InvalidGraph);
    EXPECT_NE(placed.error().message.find("device 1 in another SYCL context"), std::string::npos)
        << placed.error().message;

    node->place_on(1);
    EXPECT_EQ(placement_of(*node, memory).value(), 1u) << "the owner itself is never refused";

    memory.forget(&cell);
}

TEST(Medium_NodePlacementContexts, AStatedIndexPastTheDevicesIsLeftToTheDispatch)
{
    SushiRuntime::Internal::Memory::USMAllocator memory(two_queues_in_two_contexts());
    int cell = 0;
    memory.adopt(&cell, 1, sizeof(cell));

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(5);
    EXPECT_EQ(placement_of(*node, memory).value(), 5u);

    memory.forget(&cell);
}

TEST_F(Medium_NodePlacementRule, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(&cell);
    EXPECT_EQ(placement_of(*node, memory_).value(), 0u);

    node->place_on(1);
    EXPECT_EQ(placement_of(*node, memory_).value(), 1u);
}
