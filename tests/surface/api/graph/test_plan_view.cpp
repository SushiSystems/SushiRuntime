/**************************************************************************/
/* test_plan_view.cpp                                                     */
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
 * @file test_plan_view.cpp
 * @brief Tests compile(), which builds a plan without running it, and Graph::plan().
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every buffer here. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 32;

    /** @brief Allocates a buffer of K_WIDTH zeroes. */
    Buffer<Word> zeroed(const Runtime& rt)
    {
        auto buffer = rt.buffer<Word>(K_WIDTH).value();
        for (std::size_t i = 0; i < K_WIDTH; ++i)
            buffer[i] = 0u;
        return buffer;
    }
} // namespace

/** @brief Fixture of the plan-view tests, which record graphs on a host runtime. */
class Small_PlanView : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the plan-view tests of a chain of three kernels whose second node states every option. */
class Small_PlanViewChain : public TestSupport::HostRuntimeTest
{
    protected:
        /** @brief Records the chain. */
        void SetUp() override
        {
            first_.emplace(zeroed(runtime()));
            second_.emplace(zeroed(runtime()));
            third_.emplace(zeroed(runtime()));
            Word* a = first_->data();
            Word* b = second_->data();
            Word* c = third_->data();

            NodeOptions scaled;
            scaled.name = "scale";
            scaled.operation = "plan_view.scale"_op;
            scaled.placement_class = "sushiai.replica"_class;
            scaled.placement_key = 0;
            scaled.determinism = Determinism::Bitwise;

            graph_.emplace(runtime().graph());
            graph_->add(K_WIDTH, Reads(), Writes(*first_), [a](std::size_t i) { a[i] = 1u; });
            graph_->add(K_WIDTH, Reads(*first_), Writes(*second_),
                        [a, b](std::size_t i) { b[i] = a[i] * 2u; }, scaled);
            graph_->add(K_WIDTH, Reads(*second_), Writes(*third_),
                        [b, c](std::size_t i) { c[i] = b[i]; });
        }

        std::optional<Buffer<Word>> first_;
        std::optional<Buffer<Word>> second_;
        std::optional<Buffer<Word>> third_;
        std::optional<Graph> graph_;
};

/** @brief Tests that compile() builds the plan and runs nothing. */
TEST_F(Small_PlanView, CompileRunsNothing)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    auto graph = runtime().graph();
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(graph.compile());
    EXPECT_EQ(graph.compile_count(), 1u);
    EXPECT_EQ(words[0], 0u);
}

/** @brief Tests that a plan compile() built, and the graph has not changed since, is not built again. */
TEST_F(Small_PlanView, ACurrentPlanIsNotBuiltAgain)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    auto graph = runtime().graph();
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(graph.compile());
    ASSERT_TRUE(graph.compile());
    EXPECT_EQ(graph.compile_count(), 1u);
}

/** @brief Tests that run() after compile() uses the plan compile() built. */
TEST_F(Small_PlanView, RunUsesThePlanCompileBuilt)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    auto graph = runtime().graph();
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(graph.compile());
    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 1u);
    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(words[i], 1u) << "at i=" << i;
}

/** @brief Tests that compile() and plan() return the recorder's InvalidAccess refusal, the one run() returns. */
TEST_F(Small_PlanView, CompileReturnsTheRecordersRefusal)
{
    auto graph = runtime().graph();
    State<int> empty;
    graph.add_rule(empty, [](std::size_t, const int*, int*) {});

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, ErrorCode::InvalidAccess);
    EXPECT_FALSE(compiled.error().message.empty());
    EXPECT_EQ(graph.compile_count(), 0u);

    const auto plan = graph.plan();
    ASSERT_FALSE(plan);
    EXPECT_EQ(plan.error().code, ErrorCode::InvalidAccess);
    EXPECT_EQ(plan.error().message, compiled.error().message);

    const auto run = graph.run();
    ASSERT_FALSE(run);
    EXPECT_EQ(run.error().message, compiled.error().message);
}

/** @brief Tests that compile() on a graph with nothing recorded is InvalidGraph, as run() is. */
TEST_F(Small_PlanView, CompileOfAnEmptyGraphIsRefused)
{
    auto graph = runtime().graph();

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, ErrorCode::InvalidGraph);
}

/** @brief Tests that compile() on a DynamicGraph composes it once and runs nothing. */
TEST_F(Small_PlanView, ACompiledDynamicGraphRunsNothing)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    auto world = runtime().dynamic_graph();
    world.region(0).add(K_WIDTH, Reads(), Writes(target),
                        [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(world.compile());
    EXPECT_EQ(world.compose_count(), 1u);
    EXPECT_EQ(words[0], 0u);
}

/** @brief Tests that a DynamicGraph compiled and then run composes once in all. */
TEST_F(Small_PlanView, ADynamicGraphCompiledThenRunComposesOnce)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    auto world = runtime().dynamic_graph();
    world.region(0).add(K_WIDTH, Reads(), Writes(target),
                        [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(world.compile());
    ASSERT_TRUE(world.run());
    EXPECT_EQ(world.compose_count(), 1u);
    EXPECT_EQ(words[0], 1u);
}

/** @brief Tests that plan() of a chain of three kernels reports its predecessors and regions, and runs nothing. */
TEST_F(Small_PlanViewChain, AChainOfThreeKernelsReportsItsPredecessorsAndRegions)
{
    const auto plan = graph_->plan();
    ASSERT_TRUE(plan);
    EXPECT_EQ(graph_->compile_count(), 1u);
    EXPECT_EQ(first_->data()[0], 0u) << "plan() must run nothing";
    ASSERT_EQ(plan->nodes.size(), 3u);

    const std::size_t expected_predecessors[] = {0u, 1u, 1u};
    const void* const expected_reads[] = {nullptr, first_->data(), second_->data()};
    const void* const expected_writes[] = {first_->data(), second_->data(), third_->data()};
    for (std::size_t node = 0; node < 3u; ++node)
    {
        const PlanNode& described = plan->nodes[node];
        EXPECT_EQ(described.rule, node);
        EXPECT_EQ(described.predecessor_count, expected_predecessors[node]) << "node " << node;
        ASSERT_EQ(described.reads.size(), expected_reads[node] != nullptr ? 1u : 0u)
            << "node " << node;
        if (expected_reads[node] != nullptr)
            EXPECT_EQ(described.reads[0].base, expected_reads[node]) << "node " << node;
        ASSERT_EQ(described.writes.size(), 1u) << "node " << node;
        EXPECT_EQ(described.writes[0].base, expected_writes[node]) << "node " << node;
    }
}

/** @brief Tests that plan() carries the name, operation, placement and determinism a node stated. */
TEST_F(Small_PlanViewChain, ThePlanCarriesTheOptionsANodeStated)
{
    const auto plan = graph_->plan();
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->nodes.size(), 3u);

    EXPECT_EQ(plan->nodes[1].name, std::string("scale"));
    EXPECT_TRUE(plan->nodes[1].operation == "plan_view.scale"_op);
    EXPECT_FALSE(plan->nodes[0].operation.is_named());
    EXPECT_EQ(plan->nodes[1].placement_class, "sushiai.replica"_class);
    ASSERT_TRUE(plan->nodes[1].placement_key.has_value());
    EXPECT_EQ(*plan->nodes[1].placement_key, 0u);
    EXPECT_FALSE(plan->nodes[0].placement_class.is_named());
    EXPECT_FALSE(plan->nodes[0].placement_key.has_value());
    EXPECT_EQ(plan->nodes[1].determinism, Determinism::Bitwise);
    EXPECT_EQ(plan->nodes[0].determinism, Determinism::Unstated);
}

/** @brief Tests that a PlanView stays readable after its graph is destroyed. */
TEST_F(Small_PlanView, AViewOutlivesItsGraph)
{
    auto target = zeroed(runtime());
    Word* words = target.data();
    PlanView kept;
    {
        NodeOptions named;
        named.name = "kept";
        auto graph = runtime().graph();
        graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] = 1u; },
                  named);
        kept = graph.plan().value();
    }

    ASSERT_EQ(kept.nodes.size(), 1u);
    EXPECT_EQ(kept.nodes[0].name, std::string("kept"));
    EXPECT_EQ(kept.nodes[0].writes[0].base, static_cast<void*>(words));
}
