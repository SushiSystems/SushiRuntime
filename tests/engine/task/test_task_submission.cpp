/**************************************************************************/
/* test_task_submission.cpp                                               */
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
 * @file test_task_submission.cpp
 * @brief Tests submit_task() and submit_task_discarding() on host and kernel tasks.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/dispatch_events.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/task_submission.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Graph::SmallEventVector;

namespace
{
    /** @brief What a node's body saw when the submission ran it. */
    struct BodyLog
    {
        /** @brief Times the body ran. */
        int runs = 0;
        /** @brief Dependencies the host body received. */
        std::size_t seen_dependencies = 0;
    };

    /** @brief Builds a host node whose body logs into @p log, submits one kernel and hands its event over. */
    sushi_ptr<Node> host_node(BodyLog& log)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::Host;
        node->host_work = [&log](sycl::queue& queue, const std::vector<sycl::event>& dependencies)
        {
            ++log.runs;
            log.seen_dependencies = dependencies.size();
            const sycl::event event = queue.submit([&dependencies](sycl::handler& h)
            {
                h.depends_on(dependencies);
                h.single_task([]() {});
            });
            Internal::DispatchEvents::DispatchEventScope::note(event);
            return event;
        };
        return node;
    }

    /** @brief Builds a kernel node whose command group logs into @p log. */
    sushi_ptr<Node> kernel_node(BodyLog& log)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::Kernel;
        node->work = [&log](sycl::handler& h)
        {
            ++log.runs;
            h.single_task([]() {});
        };
        return node;
    }

    /** @brief Builds a vector of @p count completed events from @p queue. */
    SmallEventVector completed_events(sycl::queue& queue, std::size_t count)
    {
        std::vector<sycl::event> events;
        for (std::size_t i = 0; i < count; ++i)
            events.push_back(queue.submit([](sycl::handler& h) { h.single_task([]() {}); }));
        SmallEventVector small;
        small = events;
        return small;
    }
} // namespace

/** @brief Fixture of the tests that submit to the queue of the program. */
class Medium_TaskSubmission : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a host task receives every dependency. */
TEST_F(Medium_TaskSubmission, AHostTaskGetsItsDependencies)
{
    const SmallEventVector dependencies = completed_events(queue(), 3);
    BodyLog log;
    const sushi_ptr<Node> task = host_node(log);
    std::vector<sycl::event> handed_over;

    Internal::Graph::submit_task(queue(), *task, &dependencies, &handed_over).wait();

    EXPECT_EQ(log.runs, 1);
    EXPECT_EQ(log.seen_dependencies, 3u);
}

/** @brief Tests that the event a host task hands over lands in the sink. */
TEST_F(Medium_TaskSubmission, AHostTaskHandsItsEventOver)
{
    BodyLog log;
    const sushi_ptr<Node> task = host_node(log);
    std::vector<sycl::event> handed_over;

    Internal::Graph::submit_task(queue(), *task, nullptr, &handed_over).wait();

    EXPECT_EQ(handed_over.size(), 1u) << "the scope did not collect the handed-over event";
}

/** @brief Tests that a null sink opens no scope, so a handed-over event reaches the enclosing scope. */
TEST_F(Medium_TaskSubmission, ANullSinkCollectsNothing)
{
    BodyLog log;
    const sushi_ptr<Node> task = host_node(log);
    std::vector<sycl::event> outer;
    Internal::DispatchEvents::DispatchEventScope enclosing(outer);

    Internal::Graph::submit_task(queue(), *task, nullptr, nullptr).wait();

    EXPECT_EQ(log.seen_dependencies, 0u);
    EXPECT_EQ(outer.size(), 1u) << "a null sink must leave the enclosing scope in force";
}

/** @brief Tests that a kernel task runs its command group once and returns an event. */
TEST_F(Medium_TaskSubmission, AKernelTaskReturnsAnEvent)
{
    const SmallEventVector dependencies = completed_events(queue(), 2);
    BodyLog log;
    const sushi_ptr<Node> task = kernel_node(log);
    std::vector<sycl::event> handed_over;

    Internal::Graph::submit_task(queue(), *task, &dependencies, &handed_over).wait();

    EXPECT_EQ(log.runs, 1);
}

/** @brief Tests that a kernel task hands no event over. */
TEST_F(Medium_TaskSubmission, AKernelTaskHandsNoEventOver)
{
    BodyLog log;
    const sushi_ptr<Node> task = kernel_node(log);
    std::vector<sycl::event> handed_over;

    Internal::Graph::submit_task(queue(), *task, nullptr, &handed_over).wait();

    EXPECT_TRUE(handed_over.empty()) << "a kernel task handed an event over";
}

/** @brief Tests that the discarding path runs a host task once, behind its dependencies. */
TEST_F(Medium_TaskSubmission, TheDiscardingPathRunsAHostTask)
{
    const SmallEventVector dependencies = completed_events(queue(), 1);
    BodyLog log;
    const sushi_ptr<Node> host = host_node(log);

    Internal::Graph::submit_task_discarding(queue(), *host, &dependencies);
    queue().wait();

    EXPECT_EQ(log.runs, 1);
    EXPECT_EQ(log.seen_dependencies, 1u);
}

/** @brief Tests that the discarding path runs a kernel task once. */
TEST_F(Medium_TaskSubmission, TheDiscardingPathRunsAKernelTask)
{
    BodyLog log;
    const sushi_ptr<Node> kernel = kernel_node(log);

    Internal::Graph::submit_task_discarding(queue(), *kernel, nullptr);
    queue().wait();

    EXPECT_EQ(log.runs, 1);
}
