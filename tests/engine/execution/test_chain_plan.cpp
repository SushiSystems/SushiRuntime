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
 * @file test_chain_plan.cpp
 * @brief Tests classify_chain() on in-memory placement memory: a one-device kernel chain, and what keeps a plan off the chain.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/graph/chain_plan.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/in_memory_placement_memory.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/execution/chain_plan.hpp>

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

    /** @brief Returns a task body that submits nothing and returns an empty event. */
    HostWork submits_nothing()
    {
        return [](sycl::queue&, const std::vector<sycl::event>&) { return sycl::event{}; };
    }

    /** @brief Adds a HOST node that submits device work, reading @p reads and writing @p writes. */
    Node& add_task_node(TaskGraph& graph, const std::vector<BufferRegion>& reads,
                        const std::vector<BufferRegion>& writes)
    {
        return graph.add_task(TaskMetadata{}, reads, writes, submits_nothing());
    }

    /** @brief Returns memory of @p devices devices in one context, with nothing recorded. */
    InMemoryPlacementMemory devices_in_one_context(std::size_t devices)
    {
        return InMemoryPlacementMemory(std::vector<std::size_t>(devices, 0));
    }

    /** @brief Compiles @p graph and captures its base state, as the classifier expects. */
    CompiledGraph compiled(TaskGraph& graph)
    {
        CompiledGraph plan = graph.compile().value();
        plan.capture_base_state();
        return plan;
    }
} // namespace

/** @brief Tests that two kernels on one device's memory chain in order. */
TEST(Small_ChainPlan, KernelsOnOneDeviceChainInOrder)
{
    int value = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&value, 0);
    const std::vector<BufferRegion> region{BufferRegion(&value)};
    TaskGraph graph;
    Node& first = graph.add_node(add_one(&value), region, region);
    Node& second = graph.add_node(add_one(&value), region, region);
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false);

    ASSERT_TRUE(chain.chainable);
    EXPECT_EQ(chain.queue_index, 0u);
    ASSERT_EQ(chain.order.size(), 2u);
    EXPECT_EQ(chain.order[0], &first);
    EXPECT_EQ(chain.order[1], &second);
}

/** @brief Tests that the chain submits to the device that owns the nodes' memory. */
TEST(Small_ChainPlan, TheChainTakesTheQueueOfTheDeviceThatOwnsTheMemory)
{
    int value = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(2);
    memory.record(&value, 1);
    const std::vector<BufferRegion> region{BufferRegion(&value)};
    TaskGraph graph;
    graph.add_node(add_one(&value), region, region);
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false);

    ASSERT_TRUE(chain.chainable);
    EXPECT_EQ(chain.queue_index, 1u);
}

/** @brief Tests that nodes on the memory of two devices keep the plan off the chain. */
TEST(Small_ChainPlan, KernelsOnTwoDevicesKeepThePlanOff)
{
    int on_first = 0;
    int on_second = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(2);
    memory.record(&on_first, 0);
    memory.record(&on_second, 1);
    const std::vector<BufferRegion> first_region{BufferRegion(&on_first)};
    const std::vector<BufferRegion> second_region{BufferRegion(&on_second)};
    TaskGraph graph;
    graph.add_node(add_one(&on_first), first_region, first_region);
    graph.add_node(add_one(&on_second), second_region, second_region);
    const CompiledGraph plan = compiled(graph);

    EXPECT_FALSE(classify_chain(plan, memory, false).chainable);
}

/** @brief Tests that a host node keeps the plan off the chain. */
TEST(Small_ChainPlan, AHostNodeKeepsThePlanOff)
{
    const InMemoryPlacementMemory memory = devices_in_one_context(1);
    TaskGraph graph;
    Node& host = graph.add_node(TaskWork());
    host.mode = Internal::Scheduler::Task::ExecutionMode::Host;
    host.submits_device_work_ = false;
    const CompiledGraph plan = compiled(graph);

    EXPECT_FALSE(classify_chain(plan, memory, false).chainable);
}

/** @brief Tests that a node on memory no device owns keeps the plan off the chain. */
TEST(Small_ChainPlan, ForeignMemoryKeepsThePlanOff)
{
    int foreign = 0;
    const InMemoryPlacementMemory memory = devices_in_one_context(1);
    const std::vector<BufferRegion> region{BufferRegion(&foreign)};
    TaskGraph graph;
    graph.add_node(add_one(&foreign), region, region);
    const CompiledGraph plan = compiled(graph);

    EXPECT_FALSE(classify_chain(plan, memory, false).chainable);
}

/** @brief Tests that an empty plan is not chainable. */
TEST(Small_ChainPlan, AnEmptyPlanIsNotChainable)
{
    const InMemoryPlacementMemory memory = devices_in_one_context(1);
    TaskGraph graph;
    const CompiledGraph plan = compiled(graph);

    EXPECT_FALSE(classify_chain(plan, memory, false).chainable);
}

/** @brief Tests that a task node only kernels leave unordered chains, with no opt-in. */
TEST(Small_ChainPlan, ATaskUnorderedOnlyWithAKernelChains)
{
    int task_cell = 0;
    int kernel_cell = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&task_cell, 0);
    memory.record(&kernel_cell, 0);
    const std::vector<BufferRegion> task_region{BufferRegion(&task_cell)};
    const std::vector<BufferRegion> kernel_region{BufferRegion(&kernel_cell)};
    TaskGraph graph;
    add_task_node(graph, {}, task_region);
    graph.add_node(add_one(&kernel_cell), kernel_region, kernel_region);
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false);

    ASSERT_TRUE(chain.chainable) << chain.not_chainable;
    EXPECT_EQ(chain.order.size(), 2u);
}

/** @brief Tests that two task nodes the plan orders chain in that order, with no opt-in. */
TEST(Small_ChainPlan, TwoOrderedTasksChainInOrder)
{
    int first_cell = 0;
    int second_cell = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&first_cell, 0);
    memory.record(&second_cell, 0);
    const std::vector<BufferRegion> first_region{BufferRegion(&first_cell)};
    const std::vector<BufferRegion> second_region{BufferRegion(&second_cell)};
    TaskGraph graph;
    Node& first = add_task_node(graph, {}, first_region);
    Node& second = add_task_node(graph, first_region, second_region);
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false);

    ASSERT_TRUE(chain.chainable) << chain.not_chainable;
    ASSERT_EQ(chain.order.size(), 2u);
    EXPECT_EQ(chain.order[0], &first);
    EXPECT_EQ(chain.order[1], &second);
}

/** @brief Tests that two unordered task nodes over disjoint data stay off the chain without the opt-in. */
TEST(Small_ChainPlan, TwoUnorderedTasksStayOffWithoutTheOptIn)
{
    int first_cell = 0;
    int second_cell = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&first_cell, 0);
    memory.record(&second_cell, 0);
    TaskGraph graph;
    add_task_node(graph, {}, {BufferRegion(&first_cell)});
    add_task_node(graph, {}, {BufferRegion(&second_cell)});
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false, UnorderedHostWork::Refused);

    EXPECT_FALSE(chain.chainable);
    EXPECT_STREQ(chain.not_chainable, "a chain would order host work the dependencies leave unordered");
}

/** @brief Tests that two unordered task nodes over disjoint data chain under the opt-in. */
TEST(Small_ChainPlan, TwoUnorderedTasksOverDisjointDataChainUnderTheOptIn)
{
    int first_cell = 0;
    int second_cell = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&first_cell, 0);
    memory.record(&second_cell, 0);
    TaskGraph graph;
    add_task_node(graph, {}, {BufferRegion(&first_cell)});
    add_task_node(graph, {}, {BufferRegion(&second_cell)});
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false, UnorderedHostWork::InTurnWhenDisjoint);

    ASSERT_TRUE(chain.chainable) << chain.not_chainable;
    EXPECT_EQ(chain.order.size(), 2u);
}

/** @brief Tests that two unordered task nodes reading one region stay off the chain under the opt-in. */
TEST(Small_ChainPlan, TwoUnorderedTasksReadingOneRegionStayOffUnderTheOptIn)
{
    int shared_cell = 0;
    int first_cell = 0;
    int second_cell = 0;
    InMemoryPlacementMemory memory = devices_in_one_context(1);
    memory.record(&shared_cell, 0);
    memory.record(&first_cell, 0);
    memory.record(&second_cell, 0);
    const std::vector<BufferRegion> shared_region{BufferRegion(&shared_cell)};
    TaskGraph graph;
    add_task_node(graph, shared_region, {BufferRegion(&first_cell)});
    add_task_node(graph, shared_region, {BufferRegion(&second_cell)});
    const CompiledGraph plan = compiled(graph);

    const ChainPlan chain = classify_chain(plan, memory, false, UnorderedHostWork::InTurnWhenDisjoint);

    EXPECT_FALSE(chain.chainable);
    EXPECT_STREQ(chain.not_chainable,
                 "a chain would order host work the dependencies leave unordered over shared or undeclared data");
}
