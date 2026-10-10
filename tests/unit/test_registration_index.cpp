/**************************************************************************/
/* test_registration_index.cpp                                            */
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
 * @file test_registration_index.cpp
 * @brief Tests Node::registration_index and that the nodes of one rule scope share one index.
 * @see tests/unit/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            // Test-only friend seam onto Graph::plan_ (Graph has no public accessor).
            struct RegistrationIndexTestAccess
            {
                static const SushiRuntime::Graph::CompiledGraph& plan(const Graph& g) noexcept
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
    const Node& find_named(const Graph::CompiledGraph& plan, const char* name)
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

    constexpr std::size_t WIDTH = 64;
    constexpr std::size_t RULE_NODES = 3;

    /** @brief Holds the three buffers the grouped rules pass their values through. */
    struct Stages
    {
        API::Buffer<std::uint32_t> first;  /**< The rule's input. */
        API::Buffer<std::uint32_t> second; /**< What the rule's first kernel writes. */
        API::Buffer<std::uint32_t> third;  /**< The rule's output. */
    };

    /** @brief Allocates the stage buffers and seeds the first with its indices. */
    Stages make_stages(Runtime& rt)
    {
        Stages s{rt.buffer<std::uint32_t>(WIDTH), rt.buffer<std::uint32_t>(WIDTH),
                 rt.buffer<std::uint32_t>(WIDTH)};
        for (std::size_t i = 0; i < WIDTH; ++i)
        {
            s.first[i] = static_cast<std::uint32_t>(i);
            s.second[i] = 0;
            s.third[i] = 0;
        }
        return s;
    }

    /**
     * @brief Records RULE_NODES kernels in one rule scope, each reading what the last wrote.
     * @param stages Buffers of WIDTH words; must outlive the recorder.
     */
    void record_grouped_rule(API::Recorder& recorder, const Stages& stages)
    {
        std::uint32_t* a = stages.first.data();
        std::uint32_t* b = stages.second.data();
        std::uint32_t* c = stages.third.data();

        auto rule = recorder.rule();
        recorder.kernel(WIDTH, API::Access(Reads(stages.first), Writes(stages.second)),
                        [a, b](std::size_t i) { b[i] = a[i] + 1u; });
        recorder.kernel(WIDTH, API::Access(Reads(stages.second), Writes(stages.third)),
                        [b, c](std::size_t i) { c[i] = b[i] * 2u; });
        recorder.kernel(WIDTH, API::Access(Reads(stages.third), Writes(stages.first)),
                        [c, a](std::size_t i) { a[i] = c[i]; });
    }

    /**
     * @brief Records a resolver, a device-sized kernel and a closing kernel in one rule scope.
     * @param stages Buffers of WIDTH words; must outlive the recorder.
     * @param count  Live width of the device-sized kernel; its capacity is at most WIDTH.
     */
    void record_late_bound_rule(API::Recorder& recorder, const Stages& stages,
                                const LateCount& count)
    {
        std::uint32_t* live = recorder.scratch<std::uint32_t>(
            1, stages.first.device_index(), stages.first.residency());
        const Core::ResourceRegion live_region{live, 0, sizeof(std::uint32_t)};
        std::uint32_t* a = stages.first.data();
        std::uint32_t* b = stages.second.data();
        std::uint32_t* c = stages.third.data();

        auto rule = recorder.rule();
        recorder.resolve(std::array<LateCount, 1>{count}, stages.first.device_index(),
                         API::Access(Reads(), Writes(live_region)),
                         [live](const std::array<std::size_t, 1>& value)
                         {
                             live[0] = static_cast<std::uint32_t>(value[0]);
                         });
        recorder.kernel(API::DeviceSized{count.capacity(), live, nullptr},
                        API::Access(Reads(stages.first, live_region), Writes(stages.second)),
                        [a, b](std::size_t t) { b[t] = a[t] + 1u; });
        recorder.kernel(std::size_t{1},
                        API::Access(Reads(stages.second, live_region), Writes(stages.third)),
                        [b, c, live](std::size_t) { c[0] = b[0] + live[0]; });
    }

    /** @brief Holds what a compiled plan says about one grouped rule and its two neighbours. */
    struct Grouping
    {
        std::optional<std::uint32_t> rule_index; /**< The index every rule node carries. */
        std::size_t rule_nodes = 0;              /**< Nodes that are neither neighbour. */
        std::uint32_t before_index = 0;          /**< Index of the node named "before". */
        std::uint32_t after_index = 0;           /**< Index of the node named "after". */
        std::vector<std::uint32_t> all_indices;  /**< Index of every node of the plan. */
    };

    /** @brief Reads a plan's grouping and fails when two rule nodes carry different indices. */
    Grouping grouping_of(const Graph::CompiledGraph& plan)
    {
        Grouping g;
        for (const auto& n : plan.all_nodes)
        {
            g.all_indices.push_back(n->registration_index);
            const std::string name(n->metadata.name);
            if (name == "before")
            {
                g.before_index = n->registration_index;
                continue;
            }
            if (name == "after")
            {
                g.after_index = n->registration_index;
                continue;
            }

            ++g.rule_nodes;
            if (!g.rule_index.has_value())
                g.rule_index = n->registration_index;
            EXPECT_EQ(*g.rule_index, n->registration_index)
                << "every node of one rule scope must share one registration_index.";
        }
        return g;
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
    TaskGraph graph;

    graph.add_node(noop_work());
    graph.name_last_node("A");
    graph.add_node(noop_work());
    graph.name_last_node("B");
    graph.add_node(noop_work());
    graph.name_last_node("C");

    Graph::CompiledGraph plan = graph.compile();

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
 *        them under one registration_index, the mechanism a Recorder rule scope
 *        uses so every node of one rule reports the same node_index.
 */
TEST(Unit_RegistrationIndex, ReservedIndexSharedAcrossCallsGroupsNodes)
{
    RuntimeContext ctx;
    TaskGraph graph;

    // Its own index: not part of the group below.
    graph.add_node(noop_work());
    graph.name_last_node("before");

    // Three nodes sharing one reserved index.
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

    Graph::CompiledGraph plan = graph.compile();
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
 * @brief Verifies a Recorder rule scope groups every node recorded in it under
 *        one registration_index, through the real fluent API.
 */
TEST(Unit_RegistrationIndex, RuleScopeGroupsItsNodesUnderOneIndex)
{
    using API::detail::RegistrationIndexTestAccess;

    auto rt = Runtime::create();
    Stages stages = make_stages(rt);

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
    record_grouped_rule(g.recorder(), stages);
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
    g.run();

    const Grouping grouping = grouping_of(RegistrationIndexTestAccess::plan(g));

    ASSERT_EQ(grouping.rule_nodes, RULE_NODES);
    ASSERT_TRUE(grouping.rule_index.has_value());
    EXPECT_NE(grouping.before_index, *grouping.rule_index);
    EXPECT_NE(grouping.after_index, *grouping.rule_index);
    EXPECT_EQ(grouping.after_index, grouping.before_index + 2)
        << "'after' must be before+2: the group draws exactly one index.";
}

/** @brief Tests that a rule scope's group survives a rebuild onto a fresh BackingGraph. */
TEST(Unit_RegistrationIndex, RuleScopeGroupSurvivesRebuild)
{
    using API::detail::RegistrationIndexTestAccess;

    auto rt = Runtime::create();
    Stages stages = make_stages(rt);

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
    record_grouped_rule(g.recorder(), stages);
    g.run();

    // An add() after the first run dirties the graph, so every spec is replayed; see README.md.
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
    g.run();

    const Graph::CompiledGraph& plan = RegistrationIndexTestAccess::plan(g);
    Grouping grouping = grouping_of(plan);

    ASSERT_EQ(grouping.rule_nodes, RULE_NODES);
    ASSERT_TRUE(grouping.rule_index.has_value());
    EXPECT_NE(find_named(plan, "before").registration_index, *grouping.rule_index);
    EXPECT_NE(find_named(plan, "after").registration_index, *grouping.rule_index);

    std::vector<std::uint32_t>& all_indices = grouping.all_indices;
    std::sort(all_indices.begin(), all_indices.end());
    all_indices.erase(std::unique(all_indices.begin(), all_indices.end()), all_indices.end());
    for (std::size_t i = 0; i < all_indices.size(); ++i)
        EXPECT_EQ(all_indices[i], i) << "registration indices must be dense 0..n-1 after rebuild.";
}

/**
 * @brief Tests that a rule scope groups a late-count resolver, a device-sized kernel and a
 *        plain kernel under one index.
 */
TEST(Unit_RegistrationIndex, RuleScopeGroupsResolverAndDeviceSizedNodesUnderOneIndex)
{
    using API::detail::RegistrationIndexTestAccess;

    auto rt = Runtime::create();
    Stages stages = make_stages(rt);

    const LateCount count = LateCount::from_host(WIDTH, []() { return WIDTH; });

    auto g = rt.graph();
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("before");
    record_late_bound_rule(g.recorder(), stages, count);
    g.add_untracked(std::size_t{1}, [](std::size_t) {}).name("after");
    g.run();

    const Grouping grouping = grouping_of(RegistrationIndexTestAccess::plan(g));

    ASSERT_EQ(grouping.rule_nodes, RULE_NODES);
    ASSERT_TRUE(grouping.rule_index.has_value());
    EXPECT_NE(grouping.before_index, *grouping.rule_index);
    EXPECT_NE(grouping.after_index, *grouping.rule_index);
    EXPECT_EQ(grouping.after_index, grouping.before_index + 2)
        << "'after' must be before+2: the group draws exactly one index.";
}
