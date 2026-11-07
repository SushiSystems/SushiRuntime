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
 * @file test_chain_run.cpp
 * @brief Tests ChainRun: each step submits the whole chain in order, and finish() drains it.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
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

    /** @brief Returns work that triples an integer on the device, which does not commute with adding one. */
TaskWork times_three(int* value)
{
    return [value](sycl::handler& handler)
    {
        handler.single_task([value] { *value *= 3; });
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

/** @brief Tests that each step runs its add-one node before its times-three node: two steps give 3, then 12. */
TEST(Medium_ChainRun, EachStepRunsTheWholeChain)
{
    const auto context = SushiRuntime::Test::make_context();
    int* const value = shared_integer(*context);
    ASSERT_NE(value, nullptr);
    const std::vector<BufferRegion> region{BufferRegion(value)};

    TaskGraph graph;
    graph.add_node(add_one(value), region, region);
    graph.add_node(times_three(value), region, region);
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
    EXPECT_EQ(*value, 12) << "(0 + 1) * 3 = 3, then (3 + 1) * 3 = 12; another order gives 4";
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
