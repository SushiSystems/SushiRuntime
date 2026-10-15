/**************************************************************************/
/* test_compiled_graph.cpp                                                */
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
 * @file test_compiled_graph.cpp
 * @brief Tests the dispatch width a compiled node reports.
 * @see tests/engine/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/report/run_report.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/task_work.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

static_assert(std::is_same<decltype(Node::baked_width), std::size_t>::value,
              "Node::baked_width must stay std::size_t; Task 3 widens NodeEventRecord "
              "from it and relies on this exact type.");
// Node is a non-literal type, so Node{} cannot be a static_assert operand.

namespace
{
    /** @brief A no-op device node body. */
    TaskWork noop_work()
    {
        return [](sycl::handler&) {};
    }

    /** @brief Finds the one compiled node named @p name, failing if missing or duplicated. */
    const Node& find_named(const Internal::Graph::CompiledGraph& plan, const char* name)
    {
        const Node* found = nullptr;
        for (const auto& n : plan.all_nodes)
        {
            if (std::string(n->metadata.name) == name)
            {
                EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                found = n.get();
            }
        }
        EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
        return *found;
    }
}

TEST(Small_NodeDispatchWidth, DefaultConstructedNodeReportsUnknown)
{
    const Node node;
    EXPECT_EQ(node.baked_width, std::size_t{K_WIDTH_UNKNOWN});
}

/** @brief Tests that a node added without a stamp reports the unknown width. */
TEST(Small_NodeDispatchWidth, UnstampedNodeReportsUnknown)
{
    TaskGraph graph;

    graph.add_node(noop_work());

    EXPECT_EQ(graph.last_node_added()->baked_width, std::size_t{K_WIDTH_UNKNOWN});
}

/** @brief Tests that a width set on a node and a name given after it land on that node. */
TEST(Small_NodeDispatchWidth, BakedWidthAndNameStampTheNodeTheyAreSetOn)
{
    TaskGraph graph;

    graph.add_node(noop_work()).baked_width = 4096;
    graph.name_last_node("stamped");

    ASSERT_NE(graph.last_node_added(), nullptr);
    EXPECT_EQ(graph.last_node_added()->baked_width, std::size_t{4096});
    EXPECT_STREQ(graph.last_node_added()->metadata.name, "stamped");
}

/** @brief Verifies a width set on the second of two nodes leaves the first one unknown. */
TEST(Small_NodeDispatchWidth, BakedWidthStampsOnlyTheNodeItIsSetOn)
{
    TaskGraph graph;

    graph.add_node(noop_work());
    graph.name_last_node("first");
    graph.add_node(noop_work()).baked_width = 4096;
    graph.name_last_node("second");

    Internal::Graph::CompiledGraph plan = graph.compile().value();
    ASSERT_EQ(plan.all_nodes.size(), 2u);

    EXPECT_EQ(find_named(plan, "first").baked_width, std::size_t{K_WIDTH_UNKNOWN});
    EXPECT_EQ(find_named(plan, "second").baked_width, std::size_t{4096});
}

/**
 * @brief Tests that a node without late bindings reports its baked width as both launched and live.
 */
TEST(Small_NodeDispatchWidth, BakedNodeReportsOneWidthTwice)
{
    Internal::Graph::TaskGraph graph;
    graph.add_node([](sycl::handler&) {}).baked_width = 1024;

    const Internal::Graph::NodeWidths widths = Internal::Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, 1024u);
    EXPECT_EQ(widths.live, 1024u);
}

/** @brief Tests that a dynamic node reports its capacity as launched and its live count as live. */
TEST(Small_NodeDispatchWidth, DynamicNodeSeparatesCapacityFromLiveCount)
{
    auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    bindings->count.store(129, std::memory_order_relaxed);

    Internal::Graph::TaskGraph graph;
    graph.add_node([](sycl::handler&) {}, {}, {}, bindings).baked_width = 1024;

    const Internal::Graph::NodeWidths widths = Internal::Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, 1024u);
    EXPECT_EQ(widths.live, 129u);
}

/** @brief Tests that K_USE_BAKED falls back to the baked width. */
TEST(Small_NodeDispatchWidth, UseBakedFallsBackToTheBakedWidth)
{
    auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    bindings->count.store(Internal::Graph::LateBindings::K_USE_BAKED, std::memory_order_relaxed);

    Internal::Graph::TaskGraph graph;
    graph.add_node([](sycl::handler&) {}, {}, {}, bindings).baked_width = 1024;

    EXPECT_EQ(Internal::Graph::snapshot_widths(*graph.last_node_added()).live, 1024u);
}

/** @brief Tests that a width that does not fit in uint32_t is reported as unknown. */
TEST(Small_NodeDispatchWidth, OversizedWidthReportsUnknownRatherThanTruncating)
{
    Internal::Graph::TaskGraph graph;
    graph.add_node([](sycl::handler&) {}).baked_width = std::size_t{0xFFFFFFFFu} + 1;

    const Internal::Graph::NodeWidths widths = Internal::Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, K_WIDTH_UNKNOWN);
    EXPECT_EQ(widths.live, K_WIDTH_UNKNOWN);
}
