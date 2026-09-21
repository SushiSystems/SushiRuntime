/**************************************************************************/
/* test_node_dispatch_width.cpp                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// A node's width is stamped after the node is added, the same way its name is,
// because add_node returns void and the emitters build the node before they
// know which of several internal widths it took.

#include <cstddef>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/run_report.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

static_assert(std::is_same<decltype(Node::baked_width), std::size_t>::value,
              "Node::baked_width must stay std::size_t; Task 3 widens NodeEventRecord "
              "from it and relies on this exact type.");
static_assert(noexcept(std::declval<TaskGraph&>().width_last_node(std::size_t{0})),
              "width_last_node must stay noexcept, like its name_last_node sibling.");

// Node{} cannot be a static_assert operand: "non-literal type 'Node' cannot be used in a constant expression".

namespace
{
    /** @brief A no-op device node body. */
    TaskWork noop_work()
    {
        return [](sycl::handler&) {};
    }

    /** @brief Finds the one compiled node named @p name, failing if missing or duplicated. */
    const Node& find_named(const Execution::CompiledGraph& plan, const char* name)
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

TEST(Unit_NodeDispatchWidth, DefaultConstructedNodeReportsUnknown)
{
    const Node node;
    EXPECT_EQ(node.baked_width, std::size_t{Core::WIDTH_UNKNOWN});
}

TEST(Unit_NodeDispatchWidth, UnstampedNodeReportsUnknown)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());

    EXPECT_EQ(graph.last_node_width(), std::size_t{Core::WIDTH_UNKNOWN});
}

TEST(Unit_NodeDispatchWidth, WidthLastNodeStampsTheNodeNameLastNodeNames)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());
    graph.width_last_node(4096);
    graph.name_last_node("stamped");

    EXPECT_EQ(graph.last_node_width(), std::size_t{4096});
    ASSERT_NE(graph.last_node_added(), nullptr);
    EXPECT_STREQ(graph.last_node_added()->metadata.name, "stamped");
}

/** @brief Verifies width_last_node() stamps the second of two nodes, not the first. */
TEST(Unit_NodeDispatchWidth, WidthLastNodeStampsOnlyTheMostRecentNode)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());
    graph.name_last_node("first");
    graph.add_node(noop_work());
    graph.name_last_node("second");
    graph.width_last_node(4096);

    Execution::CompiledGraph plan = graph.compile();
    ASSERT_EQ(plan.all_nodes.size(), 2u);

    EXPECT_EQ(find_named(plan, "first").baked_width, std::size_t{Core::WIDTH_UNKNOWN});
    EXPECT_EQ(find_named(plan, "second").baked_width, std::size_t{4096});
}

TEST(Unit_NodeDispatchWidth, WidthLastNodeOnAnEmptyGraphLeavesTheWidthUnknown)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.width_last_node(4096);

    EXPECT_EQ(graph.last_node_width(), std::size_t{Core::WIDTH_UNKNOWN});
    EXPECT_EQ(graph.last_node_added(), nullptr);
}

// A node with no late bindings reports its baked width as both launched and
// live, because nothing narrows it at run time.
TEST(Unit_NodeDispatchWidth, BakedNodeReportsOneWidthTwice)
{
    RuntimeContext ctx;
    Graph::TaskGraph graph(ctx);
    graph.add_node([](sycl::handler&) {});
    graph.width_last_node(1024);

    const Graph::NodeWidths widths = Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, 1024u);
    EXPECT_EQ(widths.live, 1024u);
}

// A dynamic node launches over its capacity and does work over its live count,
// and the instrument has to show both or the narrow kernels look wide.
TEST(Unit_NodeDispatchWidth, DynamicNodeSeparatesCapacityFromLiveCount)
{
    auto bindings = std::make_shared<Graph::LateBindings>();
    bindings->count.store(129, std::memory_order_relaxed);

    RuntimeContext ctx;
    Graph::TaskGraph graph(ctx);
    graph.add_node([](sycl::handler&) {}, {}, {}, bindings);
    graph.width_last_node(1024);

    const Graph::NodeWidths widths = Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, 1024u);
    EXPECT_EQ(widths.live, 129u);
}

// USE_BAKED means "the rest of capacity", not a count, so it must not be
// reported as a measured live width.
TEST(Unit_NodeDispatchWidth, UseBakedFallsBackToTheBakedWidth)
{
    auto bindings = std::make_shared<Graph::LateBindings>();
    bindings->count.store(Graph::LateBindings::USE_BAKED, std::memory_order_relaxed);

    RuntimeContext ctx;
    Graph::TaskGraph graph(ctx);
    graph.add_node([](sycl::handler&) {}, {}, {}, bindings);
    graph.width_last_node(1024);

    EXPECT_EQ(Graph::snapshot_widths(*graph.last_node_added()).live, 1024u);
}

// A width that does not fit in uint32_t is reported unknown, not truncated:
// launched_width is 32 bits wide, and a wraparound would read as a real,
// wrong measurement instead of "not representable".
TEST(Unit_NodeDispatchWidth, OversizedWidthReportsUnknownRatherThanTruncating)
{
    RuntimeContext ctx;
    Graph::TaskGraph graph(ctx);
    graph.add_node([](sycl::handler&) {});
    graph.width_last_node(std::size_t{0xFFFFFFFFu} + 1);

    const Graph::NodeWidths widths = Graph::snapshot_widths(*graph.last_node_added());
    EXPECT_EQ(widths.launched, Core::WIDTH_UNKNOWN);
    EXPECT_EQ(widths.live, Core::WIDTH_UNKNOWN);
}
