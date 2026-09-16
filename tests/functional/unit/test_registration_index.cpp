/**************************************************************************/
/* test_registration_index.cpp                                           */
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

// Unit coverage for Node::registration_index and Graph::add_reduce()'s
// grouping of its tile levels under one index (spec
// 2026-09-15-g1-node-event-records-design.md, section D, F5).

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            // Test-only friend seam onto Graph::plan_ (Graph has no public accessor).
            struct RegistrationIndexTestAccess
            {
                static const Execution::CompiledGraph& plan(const Graph& g) noexcept
                {
                    return g.plan_;
                }
            };
        }
    }
}

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

namespace
{
    /** @brief A no-op device node body; these tests check bookkeeping, not results. */
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

/**
 * @brief Verifies registration_index follows call order, not compiled order.
 *
 * Registers A, B, C in that order; compile() lists them C, B, A, and
 * registration_index must still read 0, 1, 2.
 */
TEST(Unit_RegistrationIndex, NodeIndexIsRegistrationOrder)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    graph.add_node(noop_work());
    graph.name_last_node("A");
    graph.add_node(noop_work());
    graph.name_last_node("B");
    graph.add_node(noop_work());
    graph.name_last_node("C");

    Execution::CompiledGraph plan = graph.compile();

    ASSERT_EQ(plan.all_nodes.size(), 3u);
    // Proves the premise this test depends on: compiled order is reversed.
    ASSERT_EQ(std::string(plan.all_nodes[0]->metadata.name), "C")
        << "TaskGraph::compile() no longer pops its LIFO stack in reverse; "
           "this test's premise (compiled order != registration order) no "
           "longer holds.";

    EXPECT_EQ(find_named(plan, "A").registration_index, 0u);
    EXPECT_EQ(find_named(plan, "B").registration_index, 1u);
    EXPECT_EQ(find_named(plan, "C").registration_index, 2u);

}

/**
 * @brief Verifies a reserved index passed to several add_node() calls groups
 *        them under one registration_index, the mechanism Graph::add_reduce()
 *        uses so every tile level of one call reports the same node_index.
 */
TEST(Unit_RegistrationIndex, ReservedIndexSharedAcrossCallsGroupsNodes)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    // Its own index: not part of the group below.
    graph.add_node(noop_work());
    graph.name_last_node("before");

    // Three "tile levels" sharing one reserved index.
    const std::uint32_t token = graph.reserve_registration();
    graph.add_node(noop_work(), {}, token);
    graph.name_last_node("level0");
    graph.add_node(noop_work(), {}, token);
    graph.name_last_node("level1");
    graph.add_node(noop_work(), {}, token);
    graph.name_last_node("level2");

    // Must draw a fresh index, not the group's.
    graph.add_node(noop_work());
    graph.name_last_node("after");

    Execution::CompiledGraph plan = graph.compile();
    ASSERT_EQ(plan.all_nodes.size(), 5u);

    const std::uint32_t before = find_named(plan, "before").registration_index;
    const std::uint32_t level0 = find_named(plan, "level0").registration_index;
    const std::uint32_t level1 = find_named(plan, "level1").registration_index;
    const std::uint32_t level2 = find_named(plan, "level2").registration_index;
    const std::uint32_t after  = find_named(plan, "after").registration_index;

    EXPECT_EQ(level0, token);
    EXPECT_EQ(level1, token);
    EXPECT_EQ(level2, token);
    EXPECT_NE(before, token) << "the group must not reuse an unrelated prior index.";
    EXPECT_NE(after, token) << "a node past the group must draw a fresh index.";
    EXPECT_EQ(after, before + 2)
        << "'after' must be before+2: the group draws exactly one index.";
}

/**
 * @brief Verifies Graph::add_reduce() groups every tile level under one
 *        registration_index, through the real fluent API.
 *
 * N is chosen above REDUCE_TILE^2 (256^2 = 65536) so the fixed-order plan
 * needs three levels (see Unit_FixedOrderReduce.PlanLayoutIsPureAndDisjoint
 * for the same rule at N = 1000000): 70000 -> 274 -> 2 -> 1.
 */
TEST(Unit_RegistrationIndex, AddReduceGroupsTileLevelsUnderOneIndex)
{
    using API::detail::RegistrationIndexTestAccess;

    constexpr std::size_t N = 70000;

    auto rt  = Runtime::create();
    auto in  = rt.buffer<float>(N);
    auto out = rt.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 1.0f;

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("before");
    g.add_reduce(in, out, N, Sum<float>{});
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("after");
    g.run();

    const Execution::CompiledGraph& plan = RegistrationIndexTestAccess::plan(g);

    std::optional<std::uint32_t> reduce_index;
    std::size_t reduce_node_count = 0;
    std::uint32_t before_index = 0;
    std::uint32_t after_index  = 0;

    for (const auto& n : plan.all_nodes)
    {
        const std::string name(n->metadata.name);
        if (name == "before") { before_index = n->registration_index; continue; }
        if (name == "after")  { after_index  = n->registration_index; continue; }

        ++reduce_node_count;
        if (!reduce_index.has_value())
            reduce_index = n->registration_index;
        EXPECT_EQ(*reduce_index, n->registration_index)
            << "every add_reduce() tile-level node must share one registration_index.";
    }

    ASSERT_GE(reduce_node_count, 3u) << "N = " << N << " should plan at least three tile levels.";
    ASSERT_TRUE(reduce_index.has_value());
    EXPECT_NE(before_index, *reduce_index);
    EXPECT_NE(after_index, *reduce_index);
    EXPECT_EQ(after_index, before_index + 2)
        << "'after' must be before+2: the group draws exactly one index.";
}

/**
 * @brief Verifies add_reduce()'s group survives a rebuild onto a fresh
 *        BackingGraph, whose registration counter restarts at 0.
 *
 * Adds a plain node after add_reduce(), marking the graph dirty_, then runs
 * twice so build_plan() replays the specs into a second BackingGraph. The
 * reduce's levels must still share one index, distinct from every other
 * node's, and every node's index must be dense 0..n-1 in registration order.
 */
TEST(Unit_RegistrationIndex, AddReduceGroupSurvivesRebuild)
{
    using API::detail::RegistrationIndexTestAccess;

    constexpr std::size_t N = 70000;

    auto rt  = Runtime::create();
    auto in  = rt.buffer<float>(N);
    auto out = rt.buffer<float>(1);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 1.0f;

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("before");
    g.add_reduce(in, out, N, Sum<float>{});
    g.run();

    // A second add() after the first run marks the graph dirty_, forcing
    // build_plan() to replay every spec onto a fresh BackingGraph.
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("after");
    g.run();

    const Execution::CompiledGraph& plan = RegistrationIndexTestAccess::plan(g);

    std::optional<std::uint32_t> reduce_index;
    std::size_t reduce_node_count = 0;
    std::vector<std::uint32_t> all_indices;

    for (const auto& n : plan.all_nodes)
    {
        all_indices.push_back(n->registration_index);
        const std::string name(n->metadata.name);
        if (name == "before" || name == "after")
            continue;

        ++reduce_node_count;
        if (!reduce_index.has_value())
            reduce_index = n->registration_index;
        EXPECT_EQ(*reduce_index, n->registration_index)
            << "every add_reduce() tile-level node must share one registration_index after rebuild.";
    }

    ASSERT_GE(reduce_node_count, 3u) << "N = " << N << " should plan at least three tile levels.";
    EXPECT_NE(find_named(plan, "before").registration_index, *reduce_index);
    EXPECT_NE(find_named(plan, "after").registration_index, *reduce_index);

    std::sort(all_indices.begin(), all_indices.end());
    all_indices.erase(std::unique(all_indices.begin(), all_indices.end()), all_indices.end());
    for (std::size_t i = 0; i < all_indices.size(); ++i)
        EXPECT_EQ(all_indices[i], i) << "registration indices must be dense 0..n-1 after rebuild.";
}

/**
 * @brief Verifies the LateCount overload of add_reduce() groups its tile
 *        levels, and the trailing zero-count identity write, under one index.
 *
 * Mirrors AddReduceGroupsTileLevelsUnderOneIndex, but the count is bound
 * through a LateCount host provider planned for a capacity of at least three
 * tile levels, exercising the dynamic emitter's registration token instead
 * of the fixed-count path's.
 */
TEST(Unit_RegistrationIndex, LateBoundAddReduceGroupsTileLevelsUnderOneIndex)
{
    using API::detail::RegistrationIndexTestAccess;

    constexpr std::size_t CAPACITY = 70000;

    auto rt  = Runtime::create();
    auto in  = rt.buffer<float>(CAPACITY);
    auto out = rt.buffer<float>(1);
    for (std::size_t i = 0; i < CAPACITY; ++i)
        in[i] = 1.0f;

    LateCount lc;
    lc.capacity = CAPACITY;
    lc.host = []() { return CAPACITY; };

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("before");
    g.add_reduce(in, out, lc, Sum<float>{});
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name_last("after");
    g.run();

    const Execution::CompiledGraph& plan = RegistrationIndexTestAccess::plan(g);

    std::optional<std::uint32_t> reduce_index;
    std::size_t reduce_node_count = 0;
    std::uint32_t before_index = 0;
    std::uint32_t after_index  = 0;

    for (const auto& n : plan.all_nodes)
    {
        const std::string name(n->metadata.name);
        if (name == "before") { before_index = n->registration_index; continue; }
        if (name == "after")  { after_index  = n->registration_index; continue; }

        ++reduce_node_count;
        if (!reduce_index.has_value())
            reduce_index = n->registration_index;
        EXPECT_EQ(*reduce_index, n->registration_index)
            << "every late-bound add_reduce() node (tile levels plus the "
               "zero-count write) must share one registration_index.";
    }

    ASSERT_GE(reduce_node_count, 3u)
        << "capacity = " << CAPACITY << " should plan at least three tile levels.";
    ASSERT_TRUE(reduce_index.has_value());
    EXPECT_NE(before_index, *reduce_index);
    EXPECT_NE(after_index, *reduce_index);
    EXPECT_EQ(after_index, before_index + 2)
        << "'after' must be before+2: the group draws exactly one index.";
}
