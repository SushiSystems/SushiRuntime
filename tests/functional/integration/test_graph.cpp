/**************************************************************************/
/* test_graph.cpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/graph/node.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using SushiRuntime::Test::make_order_recorder;

// Three tasks sharing one pointer must run strictly in order (RAW, then WAR/WAW).
TEST(Integration_TaskGraphTest, SequentialUSMDependency)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    int dummy_data = 0;
    void* usm_ptr = &dummy_data;

    std::vector<int> execution_order;
    std::mutex order_mutex;

    constexpr auto delay = std::chrono::milliseconds(5);

    TaskMetadata meta1{"Writer1"};
    graph.add_task(meta1, {}, {usm_ptr}, make_order_recorder(1, execution_order, order_mutex, delay));

    TaskMetadata meta2{"Reader1"};
    graph.add_task(meta2, {usm_ptr}, {}, make_order_recorder(2, execution_order, order_mutex, delay));

    TaskMetadata meta3{"Writer2"};
    graph.add_task(meta3, {}, {usm_ptr}, make_order_recorder(3, execution_order, order_mutex, delay));

    auto compiled_graph = graph.compile();
    EXPECT_EQ(compiled_graph.roots.size(), 1) << "Graph should only have 1 root due to linear dependency.";

    ctx.get_execution_engine().execute(std::move(compiled_graph));

    std::lock_guard<std::mutex> lock(order_mutex);
    ASSERT_EQ(execution_order.size(), 3);
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);
}

// Tasks with no shared memory are independent roots and run concurrently.
TEST(Integration_TaskGraphTest, IndependentRootsExecution)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    constexpr int NUM_TASKS = 10;
    std::atomic<int> completed_tasks{0};

    auto host_task = [&completed_tasks](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        completed_tasks.fetch_add(1, std::memory_order_relaxed);
        return {};
    };

    for (int i = 0; i < NUM_TASKS; ++i)
    {
        TaskMetadata meta{"IndependentNode"};
        graph.add_task(meta, {}, {}, host_task);
    }

    auto compiled_graph = graph.compile();

    EXPECT_EQ(compiled_graph.roots.size(), NUM_TASKS);

    ctx.get_execution_engine().execute(std::move(compiled_graph));

    EXPECT_EQ(completed_tasks.load(), NUM_TASKS);
}

// Diamond DAG: A(write X) -> B,C(read X) -> D(write X). B and C run in parallel
// after A; D waits for both (WAR hazard).
TEST(Integration_TaskGraphTest, FanOutFanInDiamond)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    int shared_resource = 0;
    void* ptr = &shared_resource;

    std::atomic<int> current_phase{0};
    bool fan_out_success = true;

    TaskMetadata metaA{"Node_A"};
    graph.add_task(metaA, {}, {ptr}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        current_phase.store(1, std::memory_order_release);
        return {};
    });

    auto reader_task = [&current_phase, &fan_out_success](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        if (current_phase.load(std::memory_order_acquire) < 1)
            fan_out_success = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return {};
    };

    TaskMetadata metaB{"Node_B"};
    TaskMetadata metaC{"Node_C"};
    graph.add_task(metaB, {ptr}, {}, reader_task);
    graph.add_task(metaC, {ptr}, {}, reader_task);

    TaskMetadata metaD{"Node_D"};
    graph.add_task(metaD, {}, {ptr}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        current_phase.store(3, std::memory_order_release);
        return {};
    });

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    EXPECT_TRUE(fan_out_success) << "Readers executed before the initial writer completed!";
    EXPECT_EQ(current_phase.load(), 3) << "Final writer did not complete successfully.";
}
