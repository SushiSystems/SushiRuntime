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
 * @file test_plan_cache.cpp
 * @brief Tests PlanCache through Graph: a plan is compiled once and again only when it changes.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/task/in_memory_offload_coordinator.hpp>

#include "hooked_copy_task.hpp"
#include "support/installed_offload.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::InMemoryOffloadCoordinator;

namespace
{
    /// @brief Installs a declining in-memory coordinator on a runtime for one scope.
    using InstalledCoordinator = TestSupport::InstalledOffload<InMemoryOffloadCoordinator>;
} // namespace

/** @brief Fixture of the plan-cache tests, which compile graphs on the program's runtime. */
class Medium_PlanCache : public TestSupport::RuntimeTest
{
};

/** @brief Tests that runs reuse the plan, and a new node compiles it again. */
TEST_F(Medium_PlanCache, CompilesOnlyWhenTheGraphChanges)
{
    auto graph = runtime().graph();
    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    EXPECT_EQ(graph.compile_count(), 0u);
    graph.run().value();
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 1u) << "a second run compiled the same graph again";

    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);
}

/** @brief Tests that a rule change made while a plan compiles makes the next run compile again. */
TEST_F(Medium_PlanCache, ARuleChangeDuringACompileRecompilesOnTheNextRun)
{
    const Runtime& rt = runtime();
    auto target = rt.buffer<int>(1).value();
    const auto hook = std::make_shared<std::function<void()>>();
    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(target), Testing::HookedCopyTask(hook));

    bool changed = false;
    *hook = [&rt, &changed]()
    { changed = static_cast<bool>(rt.set_placement_rules(PlacementRules{})); };
    graph.run().value();
    ASSERT_TRUE(changed) << "the compile copied the task and the change was accepted";
    EXPECT_EQ(graph.compile_count(), 1u);

    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u) << "the plan kept the generation of a newer rule set";
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 2u);
}

/** @brief Tests that a coordinator is prepared with the rules the compile read, not with a set changed during it. */
TEST_F(Medium_PlanCache, Regression_TheCoordinatorSeesTheRulesTheCompileRead)
{
    const Runtime& rt = runtime();
    const InstalledCoordinator installed(rt);
    auto target = rt.buffer<int>(1).value();
    const auto hook = std::make_shared<std::function<void()>>();
    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(target), Testing::HookedCopyTask(hook));

    bool changed = false;
    *hook = [&rt, &changed]()
    { changed = static_cast<bool>(rt.set_placement_rules(PlacementRules{})); };
    graph.run().value();
    ASSERT_TRUE(changed) << "the compile copied the task and the change was accepted";
    const std::shared_ptr<const PlacementRules> first = installed.coordinator().last_rules();

    *hook = [] {};
    graph.run().value();
    ASSERT_EQ(installed.coordinator().prepare_count(), 2u) << "the newer rule set compiled the plan again";
    EXPECT_NE(installed.coordinator().last_rules(), first)
        << "the first prepare() saw the rule set the compile had not read";
}

/** @brief Tests that a dynamic graph hands the nodes of each rebuilt region to the coordinator. */
TEST_F(Medium_PlanCache, Regression_ADynamicGraphPreparesTheNodesOfEveryRebuiltRegion)
{
    const InstalledCoordinator installed(runtime());
    auto world = runtime().dynamic_graph();
    world.region(1).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.region(2).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.run().value();
    const std::size_t first = installed.coordinator().prepare_count();
    EXPECT_GE(first, 1u) << "a region's nodes reached no coordinator";

    world.region(2).add_untracked(std::size_t{4}, [](std::size_t) {});
    world.run().value();
    EXPECT_GT(installed.coordinator().prepare_count(), first) << "the rebuilt region was not prepared again";
}

/** @brief Tests that installing or removing an offload coordinator makes the plan stale. */
TEST_F(Medium_PlanCache, Regression_InstallingOrRemovingACoordinatorRecompiles)
{
    auto graph = runtime().graph();
    graph.add_untracked(std::size_t{4}, [](std::size_t) {});
    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 1u);

    {
        const InstalledCoordinator installed(runtime());
        graph.run().value();
        EXPECT_EQ(graph.compile_count(), 2u) << "a plan compiled with no coordinator was reused";
        graph.run().value();
        EXPECT_EQ(graph.compile_count(), 2u);
    }

    graph.run().value();
    EXPECT_EQ(graph.compile_count(), 3u) << "a plan compiled under a coordinator was reused";
}
