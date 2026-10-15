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
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/graph/plumbing/plan_cache.hpp>

using namespace SushiRuntime;

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
