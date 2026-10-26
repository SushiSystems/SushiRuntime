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
#include <cstdint>
#include <memory>
#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

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

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Builds a node registered under @p index with the label @p name. */
    sushi_ptr<Node> node_of(std::uint32_t index, const char* name)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->registration_index = index;
        node->metadata.name = name;
        return node;
    }
} // namespace

/** @brief Tests that the plan builds one row per registration index, with the node's name and operation. */
TEST(Small_CompiledGraph, CapturesOneTableRowPerRegistrationIndex)
{
    CompiledGraph plan;
    plan.all_nodes.push_back(node_of(0, "load"));
    plan.all_nodes.push_back(node_of(1, "project"));
    plan.all_nodes.push_back(node_of(2, "store"));
    plan.all_nodes[1]->metadata.op_id = OperationID(0x1234u);

    plan.capture_profiling_state();

    ASSERT_NE(plan.node_table, nullptr);
    ASSERT_EQ(plan.node_table->size(), 3u);
    EXPECT_EQ(plan.node_table->find(0)->name, "load");
    EXPECT_EQ(plan.node_table->find(1)->name, "project");
    EXPECT_EQ(plan.node_table->find(1)->operation.value, 0x1234u);
    EXPECT_EQ(plan.node_table->find(2)->name, "store");
    EXPECT_FALSE(plan.node_table->find(0)->operation.is_named());
}

/** @brief Tests that several nodes sharing one index make one row that counts them. */
TEST(Small_CompiledGraph, ASharedIndexKeepsItsMultiplicity)
{
    CompiledGraph plan;
    plan.all_nodes.push_back(node_of(0, "first"));
    plan.all_nodes.push_back(node_of(1, "tile"));
    plan.all_nodes.push_back(node_of(1, "tile"));
    plan.all_nodes.push_back(node_of(1, "tile"));

    plan.capture_profiling_state();

    ASSERT_EQ(plan.node_table->size(), 2u);
    EXPECT_EQ(plan.node_table->find(0)->multiplicity, 1u);
    EXPECT_EQ(plan.node_table->find(1)->multiplicity, 3u);
    ASSERT_EQ(plan.profiling_multiplicities.size(), 2u);
    EXPECT_EQ(plan.profiling_multiplicities[1], 3u) << "the ledger sizes its rows from the same count";
}

/** @brief Tests that the first node of an index names the row and states its cost. */
TEST(Small_CompiledGraph, CarriesTheCostOfTheFirstNode)
{
    CompiledGraph plan;
    plan.all_nodes.push_back(node_of(0, "gemm"));
    plan.all_nodes.push_back(node_of(1, "copy"));
    plan.all_nodes.push_back(node_of(2, "bias"));
    plan.all_nodes[0]->metadata.cost = NodeCost{2048, 96, NodeDType::BF16};
    plan.all_nodes[1]->metadata.cost = NodeCost{};

    plan.capture_profiling_state();

    const NodeTableRow* gemm = plan.node_table->find(0);
    ASSERT_TRUE(gemm->cost.has_value());
    EXPECT_EQ(gemm->cost->flops, 2048u);
    EXPECT_EQ(gemm->cost->bytes, 96u);
    EXPECT_EQ(gemm->cost->dtype, NodeDType::BF16);
    const NodeTableRow* copy = plan.node_table->find(1);
    ASSERT_TRUE(copy->cost.has_value()) << "a stated zero is a cost";
    EXPECT_EQ(copy->cost->flops, 0u);
    EXPECT_FALSE(plan.node_table->find(2)->cost.has_value()) << "a node that stated none has none";
}

/** @brief Tests that an index no node registered under has no row, and the sentinel has none. */
TEST(Small_CompiledGraph, AnIndexNoNodeRegisteredUnderHasNoRow)
{
    CompiledGraph plan;
    plan.all_nodes.push_back(node_of(0, "a"));
    plan.all_nodes.push_back(node_of(2, "c"));
    sushi_ptr<Node> sentinel = node_of(9, "sentinel");
    sentinel->is_sentinel = true;
    plan.all_nodes.push_back(sentinel);

    plan.capture_profiling_state();

    ASSERT_EQ(plan.node_table->size(), 2u);
    EXPECT_EQ(plan.node_table->find(1), nullptr);
    EXPECT_EQ(plan.node_table->find(9), nullptr) << "the sentinel is not a node of the caller's graph";
}

/** @brief Tests that a node with no device work is marked host-native and a kernel node is not. */
TEST(Small_CompiledGraph, ANodeWithNoDeviceWorkIsHostNative)
{
    CompiledGraph plan;
    sushi_ptr<Node> native = node_of(0, "native");
    native->mode = Node::ExecutionMode::Host;
    native->submits_device_work_ = false;
    sushi_ptr<Node> kernel = node_of(1, "kernel");
    sushi_ptr<Node> task = node_of(2, "task");
    task->mode = Node::ExecutionMode::Host;
    plan.all_nodes.push_back(native);
    plan.all_nodes.push_back(kernel);
    plan.all_nodes.push_back(task);

    plan.capture_profiling_state();

    EXPECT_TRUE(plan.node_table->find(0)->host_native);
    EXPECT_FALSE(plan.node_table->find(1)->host_native);
    EXPECT_FALSE(plan.node_table->find(2)->host_native) << "a host body that submits device work is not host-native";
}

/** @brief Tests that an empty plan builds an empty table rather than none. */
TEST(Small_CompiledGraph, AnEmptyPlanBuildsAnEmptyTable)
{
    CompiledGraph plan;

    plan.capture_profiling_state();

    ASSERT_NE(plan.node_table, nullptr);
    EXPECT_EQ(plan.node_table->size(), 0u);
    EXPECT_TRUE(plan.profiling_multiplicities.empty());
}

/** @brief Tests that a second capture builds a fresh table and leaves the one a run still holds intact. */
TEST(Small_CompiledGraph, ACaptureAfterTheFirstLeavesTheEarlierTableIntact)
{
    CompiledGraph plan;
    plan.all_nodes.push_back(node_of(0, "a"));
    plan.capture_profiling_state();
    const std::shared_ptr<const NodeTable> held = plan.node_table;

    plan.all_nodes.push_back(node_of(1, "b"));
    plan.capture_profiling_state();

    EXPECT_EQ(held->size(), 1u) << "an observation keeps the table of its own run";
    EXPECT_EQ(plan.node_table->size(), 2u);
}
