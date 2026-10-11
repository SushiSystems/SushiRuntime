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
 * @brief Tests compile(), which builds a plan without running it.
 * @see modules/surface/api/tests/README.md
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

/** @brief Tests that compile() returns the refusal the recorder kept. */
TEST(Integration_PlanView, CompileReturnsTheRecordersRefusal)
{
    auto rt = Runtime::create().value();
    auto graph = rt.graph();
    graph.add_untracked(K_WIDTH, On(DeviceIndex{rt.advanced().device_count()}),
                        [](std::size_t) {});
    ASSERT_NE(graph.refusal(), nullptr);

    const auto compiled = graph.compile();
    ASSERT_FALSE(compiled);
    EXPECT_EQ(compiled.error().code, graph.refusal()->code);
    EXPECT_EQ(compiled.error().message, graph.refusal()->message);
    EXPECT_EQ(graph.compile_count(), 0u);
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
