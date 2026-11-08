/**************************************************************************/
/* test_scheduler_factory.cpp                                             */
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
 * @file test_scheduler_factory.cpp
 * @brief Tests make_scheduler(): a scheduler over a context that runs what it is given.
 * @see tests/engine/runtime_assembly/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>

#include "support/host_context.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Test;

namespace
{
    /** @brief Builds @p count host nodes that add one to @p executed and submit no device work.
     *  @param executed Must outlive the nodes. */
    std::vector<sushi_ptr<Internal::Graph::Node>> counting_nodes(std::atomic<std::size_t>& executed,
                                                                 std::size_t count)
    {
        std::vector<sushi_ptr<Internal::Graph::Node>> nodes;
        for (std::size_t index = 0; index < count; ++index)
        {
            sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
            node->mode = Internal::Graph::Node::ExecutionMode::Host;
            node->submits_device_work_ = false;
            node->native_work = [&executed] { executed.fetch_add(1, std::memory_order_relaxed); };
            nodes.push_back(std::move(node));
        }
        return nodes;
    }
} // namespace

/** @brief Tests that the scheduler runs every task submitted, then stops. */
TEST(Small_SchedulerFactory, TheSchedulerRunsWhatItIsGiven)
{
    constexpr std::size_t K_TASKS = 16;
    SeededExecutor executor{TestSupport::K_HOST_RUNTIME_SEED};
    const std::shared_ptr<Internal::Execution::RuntimeContext> context = TestSupport::make_host_context();
    Internal::Scheduler::TaskScheduler scheduler = make_scheduler(*context);
    scheduler.set_executor(&executor);
    std::atomic<std::size_t> executed{0};
    const std::vector<sushi_ptr<Internal::Graph::Node>> nodes = counting_nodes(executed, K_TASKS);

    scheduler.start();
    scheduler.arm_for_execution();
    for (const sushi_ptr<Internal::Graph::Node>& node : nodes)
        scheduler.submit(node.get());
    while (executor.run_one())
    {
    }

    scheduler.wait_quiescent();
    scheduler.stop();
    EXPECT_EQ(executed.load(), K_TASKS);
}
