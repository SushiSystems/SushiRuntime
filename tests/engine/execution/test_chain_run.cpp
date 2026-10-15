/**************************************************************************/
/* test_chain_run.cpp                                                     */
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
 * @file test_chain_run.cpp
 * @brief Tests ChainRun: each step submits the whole chain in order, and finish() drains it.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/graph/chain_plan.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/core/result.hpp>

#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/chain_run.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Returns work that adds one to an integer on the device. */
    TaskWork add_one(int* value)
    {
        return [value](sycl::handler& handler)
        {
            handler.single_task([value] { *value += 1; });
        };
    }

    /** @brief Allocates one shared integer of the context on device 0, set to 0. */
    int* shared_integer(RuntimeContext& context)
    {
        Internal::Memory::USMRequest request;
        request.kind = Internal::Memory::USMKind::Shared;
        request.bytes = sizeof(int);
        int* const value = static_cast<int*>(context.memory().allocate(request));
        if (value != nullptr)
            *value = 0;
        return value;
    }
}

/** @brief Tests that two steps of a two-node chain add four, in submission order. */
TEST(Medium_ChainRun, EachStepRunsTheWholeChain)
{
    const auto context = SushiRuntime::Test::make_context();
    int* const value = shared_integer(*context);
    ASSERT_NE(value, nullptr);
    const std::vector<BufferRegion> region{BufferRegion(value)};

    TaskGraph graph;
    graph.add_node(add_one(value), region, region);
    graph.add_node(add_one(value), region, region);
    CompiledGraph plan = graph.compile().value();
    plan.capture_base_state();
    plan.chain = classify_chain(plan, *context);
    ASSERT_TRUE(plan.chain.chainable);

    {
        ChainRun run(*context, plan);
        ASSERT_TRUE(run.submit_step(0).has_value());
        ASSERT_TRUE(run.submit_step(1).has_value());
        ASSERT_TRUE(run.finish().has_value());
    }
    EXPECT_EQ(*value, 4);
    context->memory().release(value);
}

/** @brief Tests that wait() returns once the submitted step is done. */
TEST(Medium_ChainRun, WaitReturnsAfterTheStep)
{
    const auto context = SushiRuntime::Test::make_context();
    int* const value = shared_integer(*context);
    ASSERT_NE(value, nullptr);
    const std::vector<BufferRegion> region{BufferRegion(value)};

    TaskGraph graph;
    graph.add_node(add_one(value), region, region);
    CompiledGraph plan = graph.compile().value();
    plan.capture_base_state();
    plan.chain = classify_chain(plan, *context);
    ASSERT_TRUE(plan.chain.chainable);

    ChainRun run(*context, plan);
    ASSERT_TRUE(run.submit_step(0).has_value());
    ASSERT_TRUE(run.wait().has_value());
    EXPECT_EQ(*value, 1);
    ASSERT_TRUE(run.finish().has_value());
    context->memory().release(value);
}
