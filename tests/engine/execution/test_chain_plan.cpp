/**************************************************************************/
/* test_chain_plan.cpp                                                    */
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
 * @file test_chain_plan.cpp
 * @brief Tests classify_chain(): a one-device kernel chain, and the nodes that keep a plan off the chain.
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

#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

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

/** @brief Tests that two kernels on one device's memory chain in order. */
TEST(Unit_ChainPlan, KernelsOnOneDeviceChainInOrder)
{
    const auto context = SushiRuntime::Test::make_context();
    int* const value = shared_integer(*context);
    ASSERT_NE(value, nullptr);
    const std::vector<BufferRegion> region{BufferRegion(value)};

    TaskGraph graph;
    Node& first = graph.add_node(add_one(value), region, region);
    Node& second = graph.add_node(add_one(value), region, region);
    CompiledGraph plan = graph.compile().value();
    plan.capture_base_state();

    const ChainPlan chain = classify_chain(plan, *context);
    ASSERT_TRUE(chain.chainable);
    EXPECT_EQ(chain.queue_index, 0u);
    ASSERT_EQ(chain.order.size(), 2u);
    EXPECT_EQ(chain.order[0], &first);
    EXPECT_EQ(chain.order[1], &second);
    context->memory().release(value);
}

/** @brief Tests that a host node, and a node on memory no device owns, keep the plan off the chain. */
TEST(Unit_ChainPlan, AHostNodeOrForeignMemoryKeepsThePlanOff)
{
    const auto context = SushiRuntime::Test::make_context();
    TaskGraph host_graph;
    Node& host = host_graph.add_node(TaskWork());
    host.mode = Internal::Scheduler::Task::ExecutionMode::Host;
    host.submits_device_work_ = false;
    CompiledGraph host_plan = host_graph.compile().value();
    host_plan.capture_base_state();
    EXPECT_FALSE(classify_chain(host_plan, *context).chainable);

    int foreign = 0;
    const std::vector<BufferRegion> region{BufferRegion(&foreign)};
    TaskGraph foreign_graph;
    foreign_graph.add_node(add_one(&foreign), region, region);
    CompiledGraph foreign_plan = foreign_graph.compile().value();
    foreign_plan.capture_base_state();
    EXPECT_FALSE(classify_chain(foreign_plan, *context).chainable);

    TaskGraph empty_graph;
    CompiledGraph empty_plan = empty_graph.compile().value();
    empty_plan.capture_base_state();
    EXPECT_FALSE(classify_chain(empty_plan, *context).chainable) << "an empty plan chained";
}
