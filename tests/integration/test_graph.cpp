/**************************************************************************/
/* test_graph.cpp                                                         */
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
 * @file test_graph.cpp
 * @brief Tests TaskGraph ordering for a sequential dependency, independent roots and a diamond.
 * @author Mustafa Garip
 */

#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp>

#include "test_helpers.hpp"
#include "test_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;
using SushiRuntime::Test::make_order_recorder;

/**
 * @brief Tests sequential execution due to USM dependencies.
 *
 * Three tasks sharing one pointer must run strictly in order (RAW, then WAR/WAW).
 */
TEST(Integration_TaskGraphTest, SequentialUSMDependency)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    RuntimeContext& ctx = *ctx_owner;
    TaskGraph graph;

    int dummy_data = 0;
    void* usm_ptr = &dummy_data;

    std::vector<int> execution_order;
    std::mutex order_mutex;

    constexpr auto delay = std::chrono::milliseconds(5);

    TaskMetadata meta1{"Writer1"};
    graph.add_task(meta1, {}, {SushiRuntime::BufferRegion(usm_ptr)}, make_order_recorder(1, execution_order, order_mutex, delay));

    TaskMetadata meta2{"Reader1"};
    graph.add_task(meta2, {SushiRuntime::BufferRegion(usm_ptr)}, {}, make_order_recorder(2, execution_order, order_mutex, delay));

    TaskMetadata meta3{"Writer2"};
    graph.add_task(meta3, {}, {SushiRuntime::BufferRegion(usm_ptr)}, make_order_recorder(3, execution_order, order_mutex, delay));

    auto compiled_graph = graph.compile().value();
    EXPECT_EQ(compiled_graph.roots.size(), 1) << "Graph should only have 1 root due to linear dependency.";

    ctx.execution_engine().execute(std::move(compiled_graph)).value();

    std::lock_guard<std::mutex> lock(order_mutex);
    ASSERT_EQ(execution_order.size(), 3);
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);
}

/**
 * @brief Tests concurrent execution of independent tasks.
 *
 * Tasks with no shared memory are independent roots and execute concurrently.
 */
TEST(Integration_TaskGraphTest, IndependentRootsExecution)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    RuntimeContext& ctx = *ctx_owner;
    TaskGraph graph;

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

    auto compiled_graph = graph.compile().value();

    EXPECT_EQ(compiled_graph.roots.size(), NUM_TASKS);

    ctx.execution_engine().execute(std::move(compiled_graph)).value();

    EXPECT_EQ(completed_tasks.load(), NUM_TASKS);
}

/**
 * @brief Tests execution of a diamond DAG.
 *
 * A(write X) -> B,C(read X) -> D(write X). B and C run in parallel after A; D waits for both resolving the WAR hazard.
 */
TEST(Integration_TaskGraphTest, FanOutFanInDiamond)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    RuntimeContext& ctx = *ctx_owner;
    TaskGraph graph;

    int shared_resource = 0;
    void* ptr = &shared_resource;

    std::atomic<int> current_phase{0};
    std::atomic<bool> fan_out_success{true};

    TaskMetadata metaA{"Node_A"};
    graph.add_task(metaA, {}, {SushiRuntime::BufferRegion(ptr)}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
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
    graph.add_task(metaB, {SushiRuntime::BufferRegion(ptr)}, {}, reader_task);
    graph.add_task(metaC, {SushiRuntime::BufferRegion(ptr)}, {}, reader_task);

    TaskMetadata metaD{"Node_D"};
    graph.add_task(metaD, {}, {SushiRuntime::BufferRegion(ptr)}, [&current_phase](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        current_phase.store(3, std::memory_order_release);
        return {};
    });

    auto compiled = graph.compile().value();
    ctx.execution_engine().execute(std::move(compiled)).value();

    EXPECT_TRUE(fan_out_success) << "Readers executed before the initial writer completed!";
    EXPECT_EQ(current_phase.load(), 3) << "Final writer did not complete successfully.";
}
