/**************************************************************************/
/* test_host_step_plan.cpp                                                */
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
 * @file test_host_step_plan.cpp
 * @brief Tests host_step_refusal(): which plans may run their steps on the calling thread.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <set>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/execution/host_reachable_memory.hpp>
#include <SushiRuntime/execution/host_step_plan.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Answers HostReachableMemory from the addresses a test records. */
    class TableReachableMemory final : public HostReachableMemory
    {
        public:
            /** @brief Records @p base as memory the host reaches. */
            void record(const void* base) { reachable_.insert(base); }

            /** @brief Returns whether @p base was recorded. */
            [[nodiscard]] bool host_reachable(const void* base) const override
            {
                return reachable_.count(base) != 0;
            }

        private:
            std::set<const void*> reachable_; /**< The addresses the host reaches. */
    };

    /** @brief Adds a node with a host form that writes @p cell, naming it as its region. */
    Node& add_hosted(TaskGraph& graph, int* cell)
    {
        const std::vector<BufferRegion> region{BufferRegion(cell)};
        Node& node = graph.add_node(TaskWork(), region, region);
        node.host_form = [cell]() { *cell += 1; };
        return node;
    }

    /** @brief Compiles @p graph and captures its base state. */
    CompiledGraph compiled(TaskGraph& graph)
    {
        CompiledGraph plan = graph.compile().value();
        plan.capture_base_state();
        return plan;
    }
} // namespace

/** @brief Tests that nodes with host forms on memory the host reaches are accepted. */
TEST(Small_HostStepPlan, HostFormsOnReachableMemoryAreAccepted)
{
    int cell = 0;
    TableReachableMemory memory;
    memory.record(&cell);
    TaskGraph graph;
    add_hosted(graph, &cell);
    add_hosted(graph, &cell);
    const CompiledGraph plan = compiled(graph);

    EXPECT_EQ(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that a node with no host form keeps the plan on the device. */
TEST(Small_HostStepPlan, ANodeWithNoHostFormIsRefused)
{
    int cell = 0;
    TableReachableMemory memory;
    memory.record(&cell);
    const std::vector<BufferRegion> region{BufferRegion(&cell)};
    TaskGraph graph;
    graph.add_node(TaskWork(), region, region);
    const CompiledGraph plan = compiled(graph);

    EXPECT_NE(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that a node on memory the host does not reach keeps the plan on the device. */
TEST(Small_HostStepPlan, UnreachableMemoryIsRefused)
{
    int reachable = 0;
    int unreachable = 0;
    TableReachableMemory memory;
    memory.record(&reachable);
    TaskGraph graph;
    add_hosted(graph, &reachable);
    add_hosted(graph, &unreachable);
    const CompiledGraph plan = compiled(graph);

    EXPECT_NE(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that a node that names no region keeps the plan on the device. */
TEST(Small_HostStepPlan, ANodeThatNamesNoRegionIsRefused)
{
    int cell = 0;
    const TableReachableMemory memory;
    TaskGraph graph;
    Node& node = graph.add_node(TaskWork());
    node.host_form = [&cell]() { cell += 1; };
    const CompiledGraph plan = compiled(graph);

    EXPECT_NE(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that a native host node is accepted whatever memory it names. */
TEST(Small_HostStepPlan, ANativeHostNodeIsAccepted)
{
    int cell = 0;
    const TableReachableMemory memory;
    TaskGraph graph;
    Node& node = graph.add_node(TaskWork());
    node.mode = Internal::Scheduler::Task::ExecutionMode::Host;
    node.submits_device_work_ = false;
    node.native_work = [&cell]() { cell += 1; };
    const CompiledGraph plan = compiled(graph);

    EXPECT_EQ(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that a node an offload coordinator prepared keeps the plan on the device. */
TEST(Small_HostStepPlan, AnOffloadCandidateIsRefused)
{
    int cell = 0;
    TableReachableMemory memory;
    memory.record(&cell);
    TaskGraph graph;
    add_hosted(graph, &cell).offload().offloadable = true;
    const CompiledGraph plan = compiled(graph);

    EXPECT_NE(host_step_refusal(plan, memory), nullptr);
}

/** @brief Tests that an empty plan is refused. */
TEST(Small_HostStepPlan, AnEmptyPlanIsRefused)
{
    const TableReachableMemory memory;
    TaskGraph graph;
    const CompiledGraph plan = compiled(graph);

    EXPECT_NE(host_step_refusal(plan, memory), nullptr);
}
