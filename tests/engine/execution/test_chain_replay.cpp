/**************************************************************************/
/* test_chain_replay.cpp                                                  */
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
 * @file test_chain_replay.cpp
 * @brief Tests which chained plans replay_refusal() lets a recording capture, and why it refuses the rest.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/submit_form.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/execution/chain_replay.hpp>
#include "support/handle_table.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Returns work that adds one to an integer on the device; no test runs it. */
    TaskWork add_one(int* value)
    {
        return [value](sycl::handler& handler)
        {
            handler.single_task([value] { *value += 1; });
        };
    }

    /** @brief Compiles @p graph and marks its nodes, in their order, as one chain. */
    CompiledGraph chained(TaskGraph& graph, const std::vector<Node*>& order)
    {
        CompiledGraph plan = graph.compile().value();
        plan.chain.chainable = true;
        plan.chain.order = order;
        return plan;
    }

    /** @brief Returns the refusal of a one-kernel chain whose node submits in @p form. */
    std::string refusal_of_a_kernel_in(SubmitForm form)
    {
        int value = 0;
        const std::vector<BufferRegion> region{TestSupport::region_at(&value)};
        TaskGraph graph;
        Node& node = graph.add_node(add_one(&value), region, region);
        node.submit_form = form;
        const CompiledGraph plan = chained(graph, {&node});
        const char* refused = replay_refusal(plan);
        return refused == nullptr ? std::string() : std::string(refused);
    }
} // namespace

/** @brief Tests that a plan that does not run as a chain is refused as such. */
TEST(Small_ChainReplay, APlanOffTheChainIsRefused)
{
    int value = 0;
    const std::vector<BufferRegion> region{TestSupport::region_at(&value)};
    TaskGraph graph;
    graph.add_node(add_one(&value), region, region);
    const CompiledGraph plan = graph.compile().value();

    const char* refused = replay_refusal(plan);

    ASSERT_NE(refused, nullptr);
    EXPECT_NE(std::string(refused).find("does not run as a chain"), std::string::npos) << refused;
}

/** @brief Tests that a chain of fixed kernels records. */
TEST(Small_ChainReplay, AFixedKernelRecords)
{
    EXPECT_EQ(refusal_of_a_kernel_in(SubmitForm::Fixed), "");
}

/** @brief Tests that a kernel sized from a device word is refused, naming that word. */
TEST(Small_ChainReplay, ADeviceSizedKernelIsRefused)
{
    const std::string refused = refusal_of_a_kernel_in(SubmitForm::DeviceSized);

    EXPECT_NE(refused.find("sized from a word a predecessor writes on the device"), std::string::npos) << refused;
}

/** @brief Tests that a kernel reading host state at submit is refused, since a step may change it. */
TEST(Small_ChainReplay, AStepBoundKernelIsRefused)
{
    const std::string refused = refusal_of_a_kernel_in(SubmitForm::StepBound);

    EXPECT_NE(refused.find("reads host state at submit"), std::string::npos) << refused;
}

/** @brief Tests that a task body is refused, since a recording is not shown to capture its submissions. */
TEST(Small_ChainReplay, ATaskBodyIsRefused)
{
    int value = 0;
    const std::vector<BufferRegion> region{TestSupport::region_at(&value)};
    TaskGraph graph;
    Node& kernel = graph.add_node(add_one(&value), region, region);
    kernel.submit_form = SubmitForm::Fixed;
    Node& body = graph.add_task(TaskMetadata{}, region, region,
                                [](sycl::queue&, const std::vector<sycl::event>&) { return sycl::event{}; });
    const CompiledGraph plan = chained(graph, {&kernel, &body});

    const char* refused = replay_refusal(plan);

    ASSERT_NE(refused, nullptr);
    EXPECT_NE(std::string(refused).find("task body"), std::string::npos) << refused;
}
