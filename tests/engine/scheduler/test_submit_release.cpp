/**************************************************************************/
/* test_submit_release.cpp                                                */
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
 * @file test_submit_release.cpp
 * @brief Tests which successors a kernel node's submit releases, and that its reap does not release them again.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <memory>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/scheduler/submit_release.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/task/placed_split.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Scheduler::Deque;
using SushiRuntime::Internal::Scheduler::EventPolling;
using SushiRuntime::Internal::Scheduler::K_SUBMIT_RELEASE_IN_FLIGHT;
using SushiRuntime::Internal::Scheduler::QuiescenceLatch;
using SushiRuntime::Internal::Scheduler::Task;
using SushiRuntime::Internal::Scheduler::may_release_at_submit;
using SushiRuntime::Internal::Scheduler::release_successors_at_submit;

namespace
{
    /** @brief Builds a kernel node already placed on @p device. */
    sushi_ptr<Node> placed_kernel(int device)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Task::ExecutionMode::Kernel;
        node->locality_checked_ = true;
        node->cached_queue_index_ = device;
        return node;
    }

    /** @brief Makes @p successor follow @p submitted and wait for @p outstanding predecessors. */
    void link(Node& submitted, Node& successor, int outstanding)
    {
        submitted.add_successor(&successor);
        successor.region.intra_in_degree = outstanding;
        successor.arm_in_degree();
    }

    /** @brief Builds the split a node the partitioner cut carries. */
    std::shared_ptr<const Internal::Graph::PlacedSplit> cut()
    {
        return std::make_shared<const Internal::Graph::PlacedSplit>();
    }

    /** @brief Names one way a pair is kept from a release at the predecessor's submit. */
    struct RefusalRow
    {
        const char* what;                                   /**< The refusal, as a failure names it. */
        std::function<void(Node& submitted, Node& successor)> make; /**< Turns an admitted pair into this one. */
    };
} // namespace

/** @brief Tests that two whole kernel nodes placed on one device are admitted. */
TEST(Small_SubmitRelease, TwoKernelsOnOneDeviceAreAdmitted)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);

    EXPECT_TRUE(may_release_at_submit(*submitted, *successor));
}

/** @brief Tests that each refused pair waits for the predecessor's completion. */
TEST(Small_SubmitRelease, EachRefusedPairWaitsForCompletion)
{
    const RefusalRow rows[] = {
        {"a host successor", [](Node&, Node& successor) { successor.mode = Task::ExecutionMode::Host; }},
        {"a host predecessor", [](Node& submitted, Node&) { submitted.mode = Task::ExecutionMode::Host; }},
        {"the sentinel", [](Node&, Node& successor) { successor.is_sentinel = true; }},
        {"another device", [](Node&, Node& successor) { successor.cached_queue_index_ = 1; }},
        {"an unplaced successor", [](Node&, Node& successor) { successor.locality_checked_ = false; }},
        {"an offloadable successor", [](Node&, Node& successor) { successor.offload().offloadable = true; }},
        {"a cut successor", [](Node&, Node& successor) { successor.placed_split = cut(); }},
        {"a cut predecessor", [](Node& submitted, Node&) { submitted.placed_split = cut(); }},
    };
    for (const RefusalRow& row : rows)
    {
        const sushi_ptr<Node> submitted = placed_kernel(0);
        const sushi_ptr<Node> successor = placed_kernel(0);
        row.make(*submitted, *successor);

        EXPECT_FALSE(may_release_at_submit(*submitted, *successor)) << row.what << " was admitted";
    }
}

/** @brief Tests that a submit pushes the successor it was the last outstanding predecessor of. */
TEST(Small_SubmitRelease, ASubmitPushesTheSuccessorItWasTheLastPredecessorOf)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);
    link(*submitted, *successor, 1);
    QuiescenceLatch latch;
    Deque ready;

    const std::size_t released = release_successors_at_submit(*submitted, sycl::event{}, 0, latch, ready);

    EXPECT_EQ(released, 1u);
    Task* const popped = ready.pop();
    EXPECT_EQ(popped, successor.get());
    EXPECT_NE(successor->released_after(), nullptr) << "the successor would not wait on the device";
    popped->release();
}

/** @brief Tests that a released successor holds one unit of the latch; the wait never returns otherwise. */
TEST(Small_SubmitRelease, AReleasedSuccessorHoldsOneUnitOfTheLatch)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);
    link(*submitted, *successor, 1);
    QuiescenceLatch latch;
    Deque ready;

    static_cast<void>(release_successors_at_submit(*submitted, sycl::event{}, 0, latch, ready));
    ready.pop()->release();
    latch.retire();
    latch.wait();

    SUCCEED();
}

/** @brief Tests that a successor with another outstanding predecessor is left for completion. */
TEST(Small_SubmitRelease, ASuccessorWithAnotherOutstandingPredecessorIsLeft)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);
    link(*submitted, *successor, 2);
    QuiescenceLatch latch;
    Deque ready;

    const std::size_t released = release_successors_at_submit(*submitted, sycl::event{}, 0, latch, ready);

    EXPECT_EQ(released, 0u);
    EXPECT_EQ(ready.size(), 0u);
    EXPECT_EQ(successor->in_degree.load(), 2);
}

/** @brief Tests that a worker at the in-flight bound releases nothing at a submit. */
TEST(Small_SubmitRelease, AWorkerAtTheInFlightBoundReleasesNothing)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);
    link(*submitted, *successor, 1);
    QuiescenceLatch latch;
    Deque ready;

    const std::size_t released =
        release_successors_at_submit(*submitted, sycl::event{}, K_SUBMIT_RELEASE_IN_FLIGHT, latch, ready);

    EXPECT_EQ(released, 0u);
    EXPECT_EQ(successor->in_degree.load(), 1) << "the successor was claimed and never pushed";
}

/** @brief Tests that the reap of a submitted node does not push again the successor its submit released. */
TEST(Small_SubmitRelease, TheReapDoesNotPushAReleasedSuccessorAgain)
{
    const sushi_ptr<Node> submitted = placed_kernel(0);
    const sushi_ptr<Node> successor = placed_kernel(0);
    link(*submitted, *successor, 1);
    QuiescenceLatch latch;
    Deque ready;
    static_cast<void>(release_successors_at_submit(*submitted, sycl::event{}, 0, latch, ready));
    EventPolling poller;
    EventPolling::ActiveEventList list;
    EventPolling::PolledEvent reaped;
    submitted->add_ref();
    reaped.task = submitted.get();
    reaped.ready = true;
    list.push(reaped);

    static_cast<void>(poller.poll_events(list, ready));

    EXPECT_EQ(ready.size(), 1u) << "the successor was pushed twice";
    ready.pop()->release();
}
