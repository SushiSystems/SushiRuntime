/**************************************************************************/
/* test_event_polling.cpp                                                 */
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
 * @file test_event_polling.cpp
 * @brief Tests that ActiveEventList keeps every event across its inline and spill storage, and the failures and stalls of a poll.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/in_memory_event_readiness.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>


using namespace SushiRuntime;
using SushiRuntime::Internal::Scheduler::Deque;
using SushiRuntime::Internal::Scheduler::EventPolling;
using SushiRuntime::Internal::Scheduler::InMemoryEventReadiness;
using SushiRuntime::Internal::Scheduler::QuiescenceLatch;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief Encodes an integer id into the task pointer slot as an opaque tag. */
    Task* tag(std::uintptr_t id) { return reinterpret_cast<Task*>(id); }

    /** @brief Recovers the id that tag() encoded. */
    std::uintptr_t untag(Task* t) { return reinterpret_cast<std::uintptr_t>(t); }

    /** @brief Builds an event-less entry for @p task, marked ready or not. */
    EventPolling::PolledEvent polled(Task* task, bool ready)
    {
        EventPolling::PolledEvent entry;
        entry.task = task;
        entry.ready = ready;
        return entry;
    }

    /** @brief Pushes the tags 1 to @p count, in that order, with the readiness @p ready. */
    void push_tags(EventPolling::ActiveEventList& list, std::uintptr_t count, bool ready)
    {
        for (std::uintptr_t id = 1; id <= count; ++id)
            list.push(polled(tag(id), ready));
    }

    /** @brief Removes index 0 until the list is empty and returns the tags it held, each once. */
    std::set<std::uintptr_t> drain_from_the_front(EventPolling::ActiveEventList& list)
    {
        std::set<std::uintptr_t> seen;
        while (!list.is_empty())
        {
            seen.insert(untag(list[0].task));
            list.remove_at(0);
        }
        return seen;
    }

    /** @brief Records a first failure and then every message of @p later, in order, through keep_first_failure. */
    std::optional<Error> keep_failures(const std::string& first_message, const std::vector<std::string>& later)
    {
        std::optional<Error> first;
        EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, first_message});
        for (const std::string& message : later)
            EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, message});
        return first;
    }

    /** @brief Polls until the list is empty, at most @p bound times, and returns the polls made. */
    std::size_t poll_until_empty(EventPolling& poller, EventPolling::ActiveEventList& list, Deque& queue,
                                 std::size_t bound)
    {
        std::size_t polls = 0;
        while (!list.is_empty() && polls < bound)
        {
            static_cast<void>(poller.poll_events(list, queue));
            ++polls;
        }
        return polls;
    }

    /** @brief Builds an entry that is ready, for a task the list holds one reference of. */
    EventPolling::PolledEvent ready_entry_for(Internal::Graph::Node& node)
    {
        node.add_ref();
        return polled(&node, true);
    }

    /** @brief Bounds the polls a test makes, so a defect fails the test and does not hang it. */
    constexpr std::size_t K_MAX_POLLS = 5'000'000;
} // namespace

/** @brief Tests that ActiveEventList keeps every pushed event, in push order, across inline and spill storage. */
TEST(Small_EventPolling, PushNeverDropsBeyondInlineCapacity)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 100000; // far beyond K_INLINE_CAP (256)

    push_tags(list, N, true);

    ASSERT_EQ(list.size(), N);
    EXPECT_FALSE(list.is_empty());
    // Push preserves order, so logical index i-1 holds tag i.
    EXPECT_EQ(untag(list[0].task), 1u);
    EXPECT_EQ(untag(list[N / 2].task), N / 2 + 1u);
    EXPECT_EQ(untag(list[N - 1].task), N);
}

/**
 * @brief Tests that draining the list yields exactly the pushed set, once each.
 *
 * remove_at moves the tail into the freed slot, so removing index 0 crosses the boundary between
 * inline and spilled storage repeatedly. Nothing may be lost or doubled, down to empty.
 */
TEST(Small_EventPolling, RemoveAtDrainsEveryEventExactlyOnce)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 5000; // spans inline buffer and spill
    push_tags(list, N, false);

    const std::set<std::uintptr_t> seen = drain_from_the_front(list);

    EXPECT_EQ(list.size(), 0u);
    EXPECT_TRUE(list.is_empty());
    ASSERT_EQ(seen.size(), N);
    EXPECT_EQ(*seen.begin(), 1u);
    EXPECT_EQ(*seen.rbegin(), N);
}

/** @brief Tests that removing an inline entry pulls the tail in, even when it lives in spill. */
TEST(Small_EventPolling, RemoveAcrossInlineSpillBoundary)
{
    EventPolling::ActiveEventList list;
    const std::uintptr_t N = EventPolling::ActiveEventList::K_INLINE_CAP + 1;
    push_tags(list, N, true);
    ASSERT_EQ(list.size(), N);

    // Removing an inline slot moves the one spilled entry into the freed slot.
    list.remove_at(0);

    EXPECT_EQ(list.size(), N - 1);
    EXPECT_EQ(untag(list[0].task), N);
}

/** @brief Tests that remove_at past the end changes nothing. */
TEST(Small_EventPolling, RemoveAtOutOfRangeIsANoOp)
{
    EventPolling::ActiveEventList list;
    push_tags(list, 2, true);

    list.remove_at(99);

    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(untag(list[0].task), 1u);
    EXPECT_EQ(untag(list[1].task), 2u);
}

/** @brief Tests that removing the last entry moves nothing else. */
TEST(Small_EventPolling, RemovingTheLastEntryMovesNothing)
{
    EventPolling::ActiveEventList list;
    push_tags(list, 2, true);

    list.remove_at(1);

    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(untag(list[0].task), 1u);
}

/** @brief Tests that the first failure of a poll stays, whatever follows it. */
TEST(Small_EventPolling, TheFirstFailureStays)
{
    const std::optional<Error> first = keep_failures("a", {"b", "c"});

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->message, "a");
}

/** @brief Tests that the first failure is kept without a line in the log. */
TEST(Small_EventPolling, TheFirstFailureIsLoggedNowhere)
{
    TestRunner::LogCapture capture;

    static_cast<void>(keep_failures("the first", {}));

    EXPECT_TRUE(capture.records().empty());
}

/** @brief Tests that a later failure is logged at Warn under the scheduler module. */
TEST(Small_EventPolling, ALaterFailureIsLoggedAtWarnUnderTheSchedulerModule)
{
    TestRunner::LogCapture capture;

    static_cast<void>(keep_failures("the first", {"the second"}));

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].module, "scheduler");
}

/** @brief Tests that the line of a later failure names the failure it dropped and the one it kept. */
TEST(Small_EventPolling, ALaterFailureLineNamesBothFailures)
{
    TestRunner::LogCapture capture;

    static_cast<void>(keep_failures("the first", {"the second"}));

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_NE(records[0].message.find("the second"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("the first"), std::string::npos) << records[0].message;
}

/** @brief Tests that every later failure is logged, one line each. */
TEST(Small_EventPolling, EveryLaterFailureIsLogged)
{
    TestRunner::LogCapture capture;

    static_cast<void>(keep_failures("a", {"b", "c"}));

    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 2u);
}

/** @brief Tests that a poll that finds progress counts no stall drive. */
TEST(Small_EventPolling, AnEventThatCompletesIsNotAStallDrive)
{
    EventPolling poller;
    EventPolling::ActiveEventList list;
    Deque local_queue;
    list.push(polled(nullptr, true));

    static_cast<void>(poller.poll_events(list, local_queue));

    EXPECT_TRUE(list.is_empty());
    EXPECT_EQ(poller.stall_drives(), 0u);
}

/** @brief Tests that a poll that stalls waits on its oldest event once and counts the stall. */
TEST(Small_EventPolling, CountsAStallDrive)
{
    InMemoryEventReadiness readiness;
    EventPolling poller;
    poller.set_event_readiness(&readiness);
    EventPolling::ActiveEventList list;
    Deque local_queue;
    EventPolling::PolledEvent entry = polled(nullptr, false);
    entry.has_event = true;
    list.push(entry);

    poll_until_empty(poller, list, local_queue, K_MAX_POLLS);

    EXPECT_TRUE(list.is_empty()) << "the event completed and was reaped";
    EXPECT_EQ(readiness.waits(), 2u) << "the stall waits once, the reap flushes once";
    EXPECT_EQ(poller.take_stall_drives(), 1u);
    EXPECT_EQ(poller.take_stall_drives(), 0u) << "taking the count starts it again";
}

/** @brief Tests that an event the port reports complete is reaped without a stall. */
TEST(Small_EventPolling, ACompleteEventIsReapedWithoutAStall)
{
    InMemoryEventReadiness readiness;
    readiness.complete();
    EventPolling poller;
    poller.set_event_readiness(&readiness);
    EventPolling::ActiveEventList list;
    Deque local_queue;
    EventPolling::PolledEvent entry = polled(nullptr, false);
    entry.has_event = true;
    list.push(entry);

    static_cast<void>(poller.poll_events(list, local_queue));

    EXPECT_TRUE(list.is_empty());
    EXPECT_EQ(readiness.waits(), 1u) << "the reap flushes the event once";
    EXPECT_EQ(poller.stall_drives(), 0u);
}

/** @brief Tests that a reaped task releases its successors to the queue. */
TEST(Small_EventPolling, AReapedTaskReleasesItsSuccessors)
{
    const sushi_ptr<Internal::Graph::Node> first = make_sushi<Internal::Graph::Node>();
    const sushi_ptr<Internal::Graph::Node> second = make_sushi<Internal::Graph::Node>();
    first->add_successor(second.get());
    second->region.intra_in_degree = 1;
    second->arm_in_degree();
    EventPolling poller;
    EventPolling::ActiveEventList list;
    Deque local_queue;
    list.push(ready_entry_for(*first));

    static_cast<void>(poller.poll_events(list, local_queue));

    Task* const released = local_queue.pop();
    EXPECT_EQ(released, second.get());
    released->release();
}

/** @brief Tests that a reaped task retires its own unit of the latch and counts its successor's first. */
TEST(Small_EventPolling, AReapedTaskRetiresItsOwnUnitOfTheLatch)
{
    const sushi_ptr<Internal::Graph::Node> first = make_sushi<Internal::Graph::Node>();
    const sushi_ptr<Internal::Graph::Node> second = make_sushi<Internal::Graph::Node>();
    first->add_successor(second.get());
    second->region.intra_in_degree = 1;
    second->arm_in_degree();
    QuiescenceLatch latch;
    latch.add(1);
    EventPolling poller;
    poller.set_inflight_tracker(&latch);
    EventPolling::ActiveEventList list;
    Deque local_queue;
    list.push(ready_entry_for(*first));

    static_cast<void>(poller.poll_events(list, local_queue));

    // The successor now holds one unit; the reaped task's own unit is retired, or this wait never returns.
    local_queue.pop()->release();
    latch.retire();
    bool drained = false;
    latch.wait();
    drained = true;
    EXPECT_TRUE(drained);
}
