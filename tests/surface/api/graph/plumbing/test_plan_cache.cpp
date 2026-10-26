/**************************************************************************/
/* test_plan_cache.cpp                                                    */
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
 * @file test_plan_cache.cpp
 * @brief Tests PlanCache through Graph: a plan is compiled once and again only when it changes.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "run_recorder.hpp"
#include <SushiRuntime/api/graph/plumbing/plan_cache.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

#include "hooked_copy_task.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Declines every node, so a run with it installed runs all of them locally. */
    class DecliningCoordinator : public Internal::Graph::OffloadCoordinator
    {
        public:
            /** @brief Keeps no sink; it never hands a node back. */
            void bind(Internal::Graph::OffloadSink&) override
            {

            }

            /** @brief Marks no node offloadable. */
            void prepare(span<const sushi_ptr<Internal::Graph::Node>>,
                         const std::shared_ptr<const PlacementRules>&) override
            {

            }

            /** @brief Declines the node. */
            bool try_offload(Internal::Graph::Node&) override { return false; }

            /** @brief Ignores a node the pool ran. */
            void on_local_completion(Internal::Graph::Node&) noexcept override
            {

            }

            /** @brief Has no remote work to end. */
            void stop() noexcept override
            {

            }
    };

    /** @brief Declines every node and keeps the rules its prepare() was given. */
    class RuleRecordingCoordinator final : public DecliningCoordinator
    {
        public:
            /** @brief Keeps the rules of the compile that prepared the nodes. */
            void prepare(span<const sushi_ptr<Internal::Graph::Node>>,
                         const std::shared_ptr<const PlacementRules>& rules) override
            {
                prepared_rules = rules;
            }

            /// @brief The rules of the last prepare(); null before one.
            std::shared_ptr<const PlacementRules> prepared_rules;
    };

    /** @brief Declines every node and counts the nodes prepare() was given. */
    class NodeCountingCoordinator final : public DecliningCoordinator
    {
        public:
            /** @brief Adds the nodes of one prepare() to the count. */
            void prepare(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                         const std::shared_ptr<const PlacementRules>&) override
            {
                prepared_nodes += nodes.size();
            }

            /// @brief How many nodes every prepare() so far was given.
            std::size_t prepared_nodes = 0;
    };
} // namespace

/** @brief Tests that runs reuse the plan, and a new node or profiling compiles it again. */
TEST(Medium_PlanCache, CompilesOnlyWhenTheGraphChanges)
{
    auto runtime = Runtime::create().value();
    auto graph = runtime.graph();
    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    EXPECT_EQ(graph.compile_count(), 0u);
    graph.run().value();
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 1u) << "a second run compiled the same graph again";

    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);

    // An observer asks for device timing per run, so it rebuilds nothing.
    Testing::RunRecorder recorder(runtime);
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);
}

/** @brief Tests that a rule change made while a plan compiles makes the next run compile again. */
TEST(Medium_PlanCache, ARuleChangeDuringACompileRecompilesOnTheNextRun)
{
    auto runtime = Runtime::create().value();
    auto target = runtime.buffer<int>(1).value();
    const auto hook = std::make_shared<std::function<void()>>();
    auto graph = runtime.graph();
    graph.add_host(Reads{}, Writes(target), Testing::HookedCopyTask(hook));

    bool changed = false;
    *hook = [&runtime, &changed]()
    { changed = static_cast<bool>(runtime.set_placement_rules(PlacementRules{})); };
    graph.run().value();
    ASSERT_TRUE(changed) << "the compile copied the task and the change was accepted";
    EXPECT_EQ(graph.compile_count(), 1u);

    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u) << "the plan kept the generation of a newer rule set";
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);
}

/** @brief Tests that a coordinator is prepared with the rules the compile read, not newer ones. */
TEST(Medium_PlanCache, Regression_TheCoordinatorSeesTheRulesTheCompileRead)
{
    auto runtime = Runtime::create().value();
    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(runtime);
    const auto coordinator = std::make_shared<RuleRecordingCoordinator>();
    context.install_offload(coordinator);
    ASSERT_TRUE(static_cast<bool>(runtime.set_placement_rules(PlacementRules{})));
    const std::shared_ptr<const PlacementRules> compiled_rules = context.placement()->rules;
    ASSERT_NE(compiled_rules, nullptr);

    auto target = runtime.buffer<int>(1).value();
    const auto hook = std::make_shared<std::function<void()>>();
    auto graph = runtime.graph();
    graph.add_host(Reads{}, Writes(target), Testing::HookedCopyTask(hook));

    bool changed = false;
    *hook = [&runtime, &changed]()
    { changed = static_cast<bool>(runtime.set_placement_rules(PlacementRules{})); };
    graph.run().value();
    ASSERT_TRUE(changed) << "the compile copied the task and the change was accepted";

    EXPECT_NE(context.placement()->rules, compiled_rules) << "the change did not land";
    EXPECT_EQ(coordinator->prepared_rules, compiled_rules)
        << "prepare() saw rules newer than the ones the nodes were stamped with";
}

/** @brief Tests that a dynamic graph hands the nodes of each rebuilt region to the coordinator. */
TEST(Medium_PlanCache, Regression_ADynamicGraphPreparesTheNodesOfEveryRebuiltRegion)
{
    auto runtime = Runtime::create().value();
    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(runtime);
    const auto coordinator = std::make_shared<NodeCountingCoordinator>();
    context.install_offload(coordinator);

    auto world = runtime.dynamic_graph();
    world.region(1).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.region(2).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.run().value();
    EXPECT_EQ(coordinator->prepared_nodes, 2u) << "a region's nodes reached no coordinator";

    world.region(2).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.run().value();
    EXPECT_EQ(coordinator->prepared_nodes, 4u) << "the rebuilt region was not prepared again";
}

/** @brief Tests that installing or removing an offload coordinator makes the plan stale. */
TEST(Medium_PlanCache, Regression_InstallingOrRemovingACoordinatorRecompiles)
{
    auto runtime = Runtime::create().value();
    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(runtime);
    auto graph = runtime.graph();
    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 1u);

    context.install_offload(std::make_shared<DecliningCoordinator>());
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u) << "a plan compiled with no coordinator was reused";
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);

    context.install_offload(nullptr);
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 3u) << "a plan compiled under a coordinator was reused";
}
