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
 * @see modules/engine/task/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Errc;
    using SushiRuntime::SushiException;
    using SushiRuntime::Internal::Graph::Node;
    using SushiRuntime::Internal::Graph::placement_of;

    /** @brief Returns two queues on the device SYCL selects by default; see README.md. */
    std::vector<sycl::queue> two_queues_on_the_default_device()
    {
        const sycl::device device{sycl::default_selector_v};
        return {sycl::queue{device}, sycl::queue{device}};
    }
} // namespace

/** @brief Holds an allocator over two device entries; see README.md. */
class Unit_NodePlacementRule : public ::testing::Test
{
    protected:
        Unit_NodePlacementRule() : memory_(two_queues_on_the_default_device())
        {

        }

        /// @brief Allocates nothing; a test records ownership with adopt().
        SushiRuntime::Internal::Memory::USMAllocator memory_;
};

TEST(Unit_NodePlacementState, ANodeStatesNoPlacementUntilPlaceOn)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    EXPECT_FALSE(node->stated_placement().has_value());

    node->place_on(0);
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(0));

    node->place_on(3);
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(3));
}

TEST(Unit_NodePlacementState, PlaceOnRejectsAnIndexItCannotHold)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    node->place_on(1);

    const std::size_t too_large = Node::K_NO_PLACEMENT;
    std::optional<Errc> code;
    std::string message;
    try
    {
        node->place_on(too_large);
    }
    catch (const SushiException& failure)
    {
        code = failure.error().code;
        message = failure.what();
    }

    ASSERT_TRUE(code.has_value()) << "place_on() accepted " << too_large;
    EXPECT_EQ(*code, Errc::invalid_graph);
    EXPECT_NE(message.find(std::to_string(too_large)), std::string::npos) << message;
    EXPECT_EQ(node->stated_placement(), std::optional<std::size_t>(1));
}

TEST_F(Unit_NodePlacementRule, ANodeWithNoRegionRunsOnItsStatedDeviceElseOnTheDefault)
{
    const sushi_ptr<Node> node = make_sushi<Node>();
    EXPECT_EQ(placement_of(*node, memory_), SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT);
    EXPECT_EQ(SushiRuntime::Internal::Graph::K_DEFAULT_PLACEMENT, 0u);

    node->place_on(1);
    EXPECT_EQ(placement_of(*node, memory_), 1u);
}

TEST_F(Unit_NodePlacementRule, TheFirstWriteRegionOutranksTheFirstReadRegion)
{
    int read_cell = 0;
    int write_cell = 0;
    memory_.adopt(&read_cell, 0, sizeof(read_cell));
    memory_.adopt(&write_cell, 1, sizeof(write_cell));

    const sushi_ptr<Node> reader = make_sushi<Node>();
    reader->io_reads_.emplace_back(&write_cell);
    EXPECT_EQ(placement_of(*reader, memory_), 1u);

    const sushi_ptr<Node> both = make_sushi<Node>();
    both->io_reads_.emplace_back(&read_cell);
    both->io_writes_.emplace_back(&write_cell);
    EXPECT_EQ(placement_of(*both, memory_), 1u);

    memory_.forget(&read_cell);
    memory_.forget(&write_cell);
}

TEST_F(Unit_NodePlacementRule, AnOwnedRegionOutranksTheStatedPlacement)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell));

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);
    EXPECT_EQ(placement_of(*node, memory_), 1u);

    memory_.forget(&cell);
}

TEST_F(Unit_NodePlacementRule, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;

    const sushi_ptr<Node> node = make_sushi<Node>();
    node->io_reads_.emplace_back(&cell);
    EXPECT_EQ(placement_of(*node, memory_), 0u);

    node->place_on(1);
    EXPECT_EQ(placement_of(*node, memory_), 1u);
}
