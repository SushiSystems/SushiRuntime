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
    class DecliningCoordinator final : public Internal::Graph::OffloadCoordinator
    {
        public:
            /** @brief Keeps no sink; it never hands a node back. */
            void bind(Internal::Graph::OffloadSink&) override
            {

            }

            /** @brief Marks no node offloadable. */
            void prepare(span<const sushi_ptr<Internal::Graph::Node>>) override
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

    graph.set_profiling(true);
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 3u);
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
