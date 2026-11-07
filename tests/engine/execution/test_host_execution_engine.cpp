/**************************************************************************/
/* test_host_execution_engine.cpp                                         */
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
 * @file test_host_execution_engine.cpp
 * @brief Tests that HostExecutionEngine runs a compiled graph on a SeededExecutor in dependency order.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/execution/host_execution_engine.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>

using SushiRuntime::BufferRegion;
using SushiRuntime::ErrorCode;
using SushiRuntime::ManualClock;
using SushiRuntime::SeededExecutor;
using SushiRuntime::Internal::Execution::HostExecutionEngine;
using SushiRuntime::Internal::Graph::CompiledGraph;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Graph::TaskGraph;

namespace
{
    /** @brief Holds an engine with the executor and the clock it was made with. */
    struct HostEngine
    {
        /** @brief Makes an engine whose executor draws from a seed. */
        explicit HostEngine(std::uint64_t seed = 0)
            : executor(std::make_shared<SeededExecutor>(seed)),
              clock(std::make_shared<ManualClock>()),
              engine(executor, clock, 1, nullptr)
        {
        }

        std::shared_ptr<SeededExecutor> executor; /**< Runs the engine's nodes. */
        std::shared_ptr<ManualClock> clock;       /**< Times the engine's dispatches. */
        HostExecutionEngine engine;               /**< The engine under test. */
    };

    /**
     * @brief Builds a diamond of host nodes over four cells: a, then b and c from a, then d from b and c.
     * @param order Gains each node's letter as it runs.
     */
    CompiledGraph diamond(std::vector<char>& order, int* cells)
    {
        TaskGraph graph;
        graph.add_native({}, {BufferRegion(&cells[0])}, [&order]() { order.push_back('a'); });
        graph.add_native({BufferRegion(&cells[0])}, {BufferRegion(&cells[1])},
                         [&order]() { order.push_back('b'); });
        graph.add_native({BufferRegion(&cells[0])}, {BufferRegion(&cells[2])},
                         [&order]() { order.push_back('c'); });
        graph.add_native({BufferRegion(&cells[1]), BufferRegion(&cells[2])},
                         {BufferRegion(&cells[3])}, [&order]() { order.push_back('d'); });
        return graph.compile().value();
    }

    /** @brief Builds a graph of independent host nodes, each appending its number to order. */
    CompiledGraph siblings(std::vector<int>& order, int count)
    {
        TaskGraph graph;
        for (int node = 0; node < count; ++node)
            graph.add_native({}, {}, [&order, node]() { order.push_back(node); });
        return graph.compile().value();
    }
} // namespace

/** @brief Tests that a diamond runs its first node first and its last node last. */
TEST(Small_HostExecutionEngine, RunsADiamondInDependencyOrder)
{
    HostEngine host;
    std::vector<char> order;
    int cells[4] = {};
    CompiledGraph plan = diamond(order, cells);

    ASSERT_TRUE(host.engine.execute(plan).has_value());

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order.front(), 'a');
    EXPECT_EQ(order.back(), 'd');
}

/** @brief Tests that two engines of one seed run unordered nodes in the same order. */
TEST(Small_HostExecutionEngine, OneSeedReplaysTheOrderOfUnorderedNodes)
{
    HostEngine first(11);
    HostEngine second(11);
    std::vector<int> first_order;
    std::vector<int> second_order;
    CompiledGraph first_plan = siblings(first_order, 8);
    CompiledGraph second_plan = siblings(second_order, 8);

    ASSERT_TRUE(first.engine.execute(first_plan).has_value());
    ASSERT_TRUE(second.engine.execute(second_plan).has_value());

    EXPECT_EQ(first_order, second_order);
}

/** @brief Tests that submit runs nothing until the executor is stepped. */
TEST(Small_HostExecutionEngine, SubmitRunsNothingUntilTheExecutorIsStepped)
{
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 3);

    ASSERT_TRUE(host.engine.submit(plan).has_value());

    EXPECT_TRUE(order.empty());
    EXPECT_FALSE(host.engine.run_finished());
}

/** @brief Tests that complete drains the executor and finishes the run it submitted. */
TEST(Small_HostExecutionEngine, CompleteFinishesTheSubmittedRun)
{
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 3);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    ASSERT_TRUE(host.engine.complete().has_value());

    EXPECT_EQ(order.size(), 3u);
    EXPECT_TRUE(host.engine.run_finished());
}

/** @brief Tests that a second submit during a run is refused with InvalidState. */
TEST(Small_HostExecutionEngine, RefusesASubmitDuringARun)
{
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 2);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    auto refused = host.engine.submit(plan);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);
}

/** @brief Tests that a node that throws fails the run and its successor does not run. */
TEST(Small_HostExecutionEngine, AThrowingNodeFailsTheRunAndSkipsItsSuccessor)
{
    HostEngine host;
    int cell = 0;
    bool successor_ran = false;
    TaskGraph graph;
    graph.add_native({}, {BufferRegion(&cell)}, []() { throw std::runtime_error("boom"); });
    graph.add_native({BufferRegion(&cell)}, {}, [&successor_ran]() { successor_ran = true; });
    CompiledGraph plan = graph.compile().value();

    auto ran = host.engine.execute(plan);

    ASSERT_FALSE(ran.has_value());
    EXPECT_FALSE(successor_ran);
}

/** @brief Tests that a command group with no host form fails the run with Unsupported. */
TEST(Small_HostExecutionEngine, ACommandGroupWithNoHostFormFailsWithUnsupported)
{
    HostEngine host;
    TaskGraph graph;
    graph.add_node([](sycl::handler&) {}, std::vector<sycl::event>{});
    CompiledGraph plan = graph.compile().value();

    auto ran = host.engine.execute(plan);

    ASSERT_FALSE(ran.has_value());
    EXPECT_EQ(ran.error().code, ErrorCode::Unsupported);
}

/** @brief Tests that a command group with a host form runs that form in its place. */
TEST(Small_HostExecutionEngine, RunsTheHostFormOfACommandGroup)
{
    HostEngine host;
    int value = 0;
    TaskGraph graph;
    Node& node = graph.add_node([](sycl::handler&) {}, std::vector<sycl::event>{});
    node.host_form = [&value]() { value = 7; };
    CompiledGraph plan = graph.compile().value();

    ASSERT_TRUE(host.engine.execute(plan).has_value());

    EXPECT_EQ(value, 7);
}

/** @brief Tests that a cancelled run skips the nodes it had not run. */
TEST(Small_HostExecutionEngine, ACancelledRunSkipsTheNodesNotYetRun)
{
    HostEngine host;
    std::vector<int> order;
    CompiledGraph plan = siblings(order, 4);
    ASSERT_TRUE(host.engine.submit(plan).has_value());

    host.engine.cancel();
    static_cast<void>(host.engine.complete());

    EXPECT_TRUE(order.empty());
}
