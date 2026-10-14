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
 * @file test_plan_view.cpp
 * @brief Tests compile(), which builds a plan without running it, and Graph::plan().
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of every buffer here. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 32;

    /** @brief Allocates a buffer of K_WIDTH zeroes. */
    Buffer<Word> zeroed(Runtime& rt)
    {
        auto buffer = rt.buffer<Word>(K_WIDTH).value();
        for (std::size_t i = 0; i < K_WIDTH; ++i)
            buffer[i] = 0u;
        return buffer;
    }
} // namespace

/** @brief Tests that compile() builds the plan, runs nothing, and leaves run() no build. */
TEST(Integration_PlanView, CompileThenRunCompilesOnce)
{
    auto rt = Runtime::create().value();
    auto target = zeroed(rt);
    Word* words = target.data();
    auto graph = rt.graph();
    graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(graph.compile());
    EXPECT_EQ(graph.compile_count(), 1u);
    EXPECT_EQ(words[0], 0u) << "compile() must run nothing";

    ASSERT_TRUE(graph.compile());
    EXPECT_EQ(graph.compile_count(), 1u) << "a current plan is not built again";

    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 1u);
    for (std::size_t i = 0; i < K_WIDTH; ++i)
        ASSERT_EQ(words[i], 1u) << "at i=" << i;
}

/** @brief Tests that compile() and plan() return the refusal the recorder kept. */
TEST(Integration_PlanView, CompileReturnsTheRecordersRefusal)
{
    auto rt = Runtime::create().value();
    auto graph = rt.graph();
    State<int> empty;
    graph.add_rule(empty, [](std::size_t, const int*, int*) {});
    ASSERT_NE(graph.refusal(), nullptr);

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, graph.refusal()->code);
    EXPECT_EQ(compiled.error().message, graph.refusal()->message);
    EXPECT_EQ(graph.compile_count(), 0u);

    const auto plan = graph.plan();
    ASSERT_FALSE(plan);
    EXPECT_EQ(plan.error().message, graph.refusal()->message);
}

/** @brief Tests that compile() on a graph with nothing recorded is InvalidGraph, as run() is. */
TEST(Integration_PlanView, CompileOfAnEmptyGraphIsRefused)
{
    auto rt = Runtime::create().value();
    auto graph = rt.graph();

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, ErrorCode::InvalidGraph);
}

/** @brief Tests that a DynamicGraph composes once for compile() and the run after it. */
TEST(Integration_PlanView, ADynamicGraphCompiledThenRunComposesOnce)
{
    auto rt = Runtime::create().value();
    auto target = zeroed(rt);
    Word* words = target.data();
    auto world = rt.dynamic_graph();
    world.region(0).add(K_WIDTH, Reads(), Writes(target),
                        [words](std::size_t i) { words[i] += 1u; });

    ASSERT_TRUE(world.compile());
    EXPECT_EQ(world.compose_count(), 1u);
    EXPECT_EQ(words[0], 0u) << "compile() must run nothing";

    ASSERT_TRUE(world.run());
    EXPECT_EQ(world.compose_count(), 1u);
    EXPECT_EQ(words[0], 1u);
}

/** @brief Tests that a chain of three kernels reports its predecessors and its regions. */
TEST(Integration_PlanView, AChainOfThreeKernelsReportsItsPredecessorsAndRegions)
{
    auto rt = Runtime::create().value();
    auto first = zeroed(rt);
    auto second = zeroed(rt);
    auto third = zeroed(rt);
    Word* a = first.data();
    Word* b = second.data();
    Word* c = third.data();

    NodeOptions scaled;
    scaled.name = "scale";
    scaled.operation = "plan_view.scale"_op;

    auto graph = rt.graph();
    graph.add(K_WIDTH, Reads(), Writes(first), [a](std::size_t i) { a[i] = 1u; });
    graph.add(K_WIDTH, Reads(first), Writes(second),
              [a, b](std::size_t i) { b[i] = a[i] * 2u; }, scaled);
    graph.add(K_WIDTH, Reads(second), Writes(third), [b, c](std::size_t i) { c[i] = b[i]; });

    const auto plan = graph.plan();
    ASSERT_TRUE(plan);
    EXPECT_EQ(graph.compile_count(), 1u);
    EXPECT_EQ(a[0], 0u) << "plan() must run nothing";
    ASSERT_EQ(plan->nodes.size(), 3u);

    const std::size_t expected_predecessors[] = {0u, 1u, 1u};
    const void* const expected_reads[] = {nullptr, a, b};
    const void* const expected_writes[] = {a, b, c};
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
    EXPECT_EQ(plan->nodes[1].name, std::string("scale"));
    EXPECT_TRUE(plan->nodes[1].operation == "plan_view.scale"_op);
    EXPECT_FALSE(plan->nodes[0].operation.is_named());

    ASSERT_TRUE(graph.run());
    EXPECT_EQ(graph.compile_count(), 1u) << "the plan plan() built is the one run() uses";
    EXPECT_EQ(c[K_WIDTH - 1], 2u);
}

/** @brief Tests that a PlanView stays readable after its graph is destroyed. */
TEST(Integration_PlanView, AViewOutlivesItsGraph)
{
    auto rt = Runtime::create().value();
    auto target = zeroed(rt);
    Word* words = target.data();
    PlanView kept;
    {
        NodeOptions named;
        named.name = "kept";
        auto graph = rt.graph();
        graph.add(K_WIDTH, Reads(), Writes(target), [words](std::size_t i) { words[i] = 1u; },
                  named);
        kept = graph.plan().value();
    }

    ASSERT_EQ(kept.nodes.size(), 1u);
    EXPECT_EQ(kept.nodes[0].name, std::string("kept"));
    EXPECT_EQ(kept.nodes[0].writes[0].base, static_cast<void*>(words));
}
