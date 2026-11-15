/**************************************************************************/
/* test_host_step_run.cpp                                                 */
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
 * @file test_host_step_run.cpp
 * @brief Tests HostStepRun: the order it runs nodes in, a disabled node, and a failing node.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/execution/host_step_run.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

#include "support/host_context.hpp"
#include "support/handle_table.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Adds a node that appends @p digit to the decimal number in @p cell, on the host. */
    Node& add_digit(TaskGraph& graph, int* cell, int digit,
                    std::shared_ptr<LateBindings> bindings = nullptr)
    {
        const std::vector<BufferRegion> region{TestSupport::region_at(cell)};
        Node& node = graph.add_node(TaskWork(), region, region, std::move(bindings));
        node.host_form = [cell, digit]() { *cell = *cell * 10 + digit; };
        return node;
    }

    /** @brief Compiles @p graph, captures its base state and arms it for a step. */
    CompiledGraph armed(TaskGraph& graph)
    {
        CompiledGraph plan = graph.compile().value();
        plan.capture_base_state();
        plan.rearm();
        return plan;
    }
} // namespace

/** @brief Tests that the nodes of one step run in the order their dependencies give. */
TEST(Small_HostStepRun, NodesRunInTheOrderOfTheirDependencies)
{
    const auto context = TestSupport::make_host_context();
    int cell = 0;
    TaskGraph graph;
    add_digit(graph, &cell, 1);
    add_digit(graph, &cell, 2);
    add_digit(graph, &cell, 3);
    CompiledGraph plan = armed(graph);
    HostStepRun run(*context, plan);

    ASSERT_TRUE(run.submit_step(0).has_value());
    ASSERT_TRUE(run.finish().has_value());

    EXPECT_EQ(cell, 123);
}

/** @brief Tests that a re-armed plan runs a second step on the same run. */
TEST(Small_HostStepRun, ASecondStepRunsAfterARearm)
{
    const auto context = TestSupport::make_host_context();
    int cell = 0;
    TaskGraph graph;
    add_digit(graph, &cell, 4);
    CompiledGraph plan = armed(graph);
    HostStepRun run(*context, plan);

    ASSERT_TRUE(run.submit_step(0).has_value());
    plan.rearm();
    ASSERT_TRUE(run.submit_step(1).has_value());
    ASSERT_TRUE(run.wait().has_value());

    EXPECT_EQ(cell, 44);
}

/** @brief Tests that a disabled late-bound node runs nothing and still frees its successor. */
TEST(Small_HostStepRun, ADisabledNodeRunsNothingAndFreesItsSuccessor)
{
    const auto context = TestSupport::make_host_context();
    int cell = 0;
    auto bindings = std::make_shared<LateBindings>();
    bindings->enabled.store(false);
    TaskGraph graph;
    add_digit(graph, &cell, 1, bindings);
    add_digit(graph, &cell, 2);
    CompiledGraph plan = armed(graph);
    HostStepRun run(*context, plan);

    ASSERT_TRUE(run.submit_step(0).has_value());

    EXPECT_EQ(cell, 2);
}

/** @brief Tests that a node that throws ends the step with an error and its successor never runs. */
TEST(Small_HostStepRun, AFailingNodeEndsTheStepBeforeItsSuccessor)
{
    const auto context = TestSupport::make_host_context();
    int cell = 0;
    TaskGraph graph;
    Node& failing = add_digit(graph, &cell, 1);
    failing.host_form = []() { throw std::runtime_error("host form failed"); };
    add_digit(graph, &cell, 2);
    CompiledGraph plan = armed(graph);
    HostStepRun run(*context, plan);

    const auto stepped = run.submit_step(0);

    ASSERT_FALSE(stepped.has_value());
    EXPECT_NE(stepped.error().message.find("host form failed"), std::string::npos);
    EXPECT_EQ(cell, 0);
}
