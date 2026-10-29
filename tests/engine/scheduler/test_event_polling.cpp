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
 * @file test_event_polling.cpp
 * @brief Tests that ActiveEventList keeps every event across its inline and spill storage.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/task/task.hpp>
#include <atomic>
#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <sycl/sycl.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using SushiRuntime::Internal::Scheduler::EventPolling;
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
}

/**
 * @brief Pushing past the inline capacity stores every event, none dropped.
 */
TEST(Small_EventPolling, PushNeverDropsBeyondInlineCapacity)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 100000; // far beyond K_INLINE_CAP (256)

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push(polled(tag(i), true));

    ASSERT_EQ(list.size(), N);
    EXPECT_FALSE(list.is_empty());

    // Push preserves order, so logical index i-1 holds tag i.
    EXPECT_EQ(untag(list[0].task), 1u);
    EXPECT_EQ(untag(list[N / 2].task), N / 2 + 1u);
    EXPECT_EQ(untag(list[N - 1].task), N);
}

/**
 * @brief Draining the list yields exactly the pushed set, once each.
 *
 * remove_at reorders via swap-with-last, so identities move around; what must
 * hold is that the multiset of survivors is exactly what went in, with no loss
 * and no duplication, all the way down to empty.
 */
TEST(Small_EventPolling, RemoveAtDrainsEveryEventExactlyOnce)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 5000; // spans inline buffer and spill

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push(polled(tag(i), false));

    std::set<std::uintptr_t> seen;
    // Removing index 0 relocates the tail into the front on every step; see README.md.
    while (!list.is_empty())
    {
        seen.insert(untag(list[0].task));
        list.remove_at(0);
    }

    EXPECT_EQ(list.size(), 0u);
    EXPECT_TRUE(list.is_empty());
    ASSERT_EQ(seen.size(), N);
    EXPECT_EQ(*seen.begin(), 1u);
    EXPECT_EQ(*seen.rbegin(), N);
}

/**
 * @brief Removing an inline entry pulls the tail in, even when it lives in spill.
 */
TEST(Small_EventPolling, RemoveAcrossInlineSpillBoundary)
{
    EventPolling::ActiveEventList list;
    const std::uintptr_t N = EventPolling::ActiveEventList::K_INLINE_CAP + 1;

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push(polled(tag(i), true));

    ASSERT_EQ(list.size(), N);

    // Removing an inline slot moves the one spilled entry into the freed slot; see README.md.
    list.remove_at(0);

    EXPECT_EQ(list.size(), N - 1);
    EXPECT_EQ(untag(list[0].task), N);
}

/**
 * @brief Out-of-range remove_at is a no-op, and removing the tail needs no move.
 */
TEST(Small_EventPolling, RemoveAtEdgeCases)
{
    EventPolling::ActiveEventList list;
    list.push(polled(tag(10), true));
    list.push(polled(tag(20), true));

    list.remove_at(99); // out of range
    EXPECT_EQ(list.size(), 2u);

    list.remove_at(1); // remove the tail itself
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(untag(list[0].task), 10u);
}

using namespace SushiRuntime;
using SushiRuntime::Internal::Scheduler::EventPolling;
using SushiRuntime::Internal::Scheduler::Task;

namespace
{
    /** @brief A queue that takes no successor, since the polled tasks have none. */
    struct NoSuccessors
    {
        /** @brief Never called: the polled entries name no task. */
        void push(Task*) {}
    };

    /** @brief Bounds the polls a test makes, so a defect fails the test and does not hang it. */
    constexpr std::size_t K_MAX_POLLS = 5'000'000;
} // namespace

/** @brief Tests that the first failure of a poll is kept and a later one is logged, not returned. */
TEST(Small_EventPolling, KeepsTheFirstFailureAndLogsTheSecond)
{
    TestRunner::LogCapture capture;
    std::optional<Error> first;

    EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, "the first"});
    EXPECT_TRUE(capture.records().empty()) << "the first failure is kept without a line";

    EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, "the second"});

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->message, "the first") << "a later failure must not replace the first";
    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].module, "scheduler");
    EXPECT_NE(records[0].message.find("the second"), std::string::npos) << records[0].message;
    EXPECT_NE(records[0].message.find("the first"), std::string::npos) << records[0].message;
}

/** @brief Tests that a third failure is logged too and the first stays. */
TEST(Small_EventPolling, EveryLaterFailureIsLoggedAndTheFirstStays)
{
    TestRunner::LogCapture capture;
    std::optional<Error> first;

    EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, "a"});
    EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, "b"});
    EventPolling::keep_first_failure(first, Error{ErrorCode::KernelFailed, "c"});

    EXPECT_EQ(first->message, "a");
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 2u);
}

/** @brief Tests that a poll that stalls waits on its oldest event once and counts it. */
TEST(Medium_EventPolling, CountsAStallDrive)
{
    sycl::queue queue{sycl::cpu_selector_v};
    EventPolling poller;
    EventPolling::ActiveEventList list;
    NoSuccessors successors;

    std::promise<void> gate;
    std::shared_future<void> opened = gate.get_future().share();
    const sycl::event pending = queue.submit([&opened](sycl::handler& handler)
    {
        SushiRuntime::Detail::enqueue_host_task(handler, [opened]() { opened.wait(); });
    });
    EventPolling::PolledEvent entry;
    entry.event = pending;
    entry.task = nullptr;
    entry.ready = false;
    entry.has_event = true;
    list.push(entry);

    // The poll that stalls counts itself before it waits, so the helper opens the gate once it sees the count.
    std::thread release([&poller, &gate]()
    {
        while (poller.stall_drives() == 0)
            std::this_thread::yield();
        gate.set_value();
    });

    std::size_t polls = 0;
    while (!list.is_empty() && polls < K_MAX_POLLS)
    {
        static_cast<void>(poller.poll_events(list, successors));
        ++polls;
    }
    release.join();
    queue.wait();

    EXPECT_TRUE(list.is_empty()) << "the event completed and was reaped";
    EXPECT_EQ(poller.take_stall_drives(), 1u);
    EXPECT_EQ(poller.take_stall_drives(), 0u) << "taking the count starts it again";
}

/** @brief Tests that a poll that finds progress counts no stall drive. */
TEST(Medium_EventPolling, AnEventThatCompletesIsNotAStallDrive)
{
    EventPolling poller;
    EventPolling::ActiveEventList list;
    NoSuccessors successors;
    EventPolling::PolledEvent entry;
    entry.task = nullptr;
    entry.ready = true;
    entry.has_event = false;
    list.push(entry);

    static_cast<void>(poller.poll_events(list, successors));

    EXPECT_TRUE(list.is_empty());
    EXPECT_EQ(poller.stall_drives(), 0u);
}
