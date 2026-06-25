/**************************************************************************/
/* test_event_polling.cpp                                                 */
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

// ActiveEventList must never drop an in-flight event: a dropped event leaves a
// task's successors un-triggered and deadlocks the run. These checks pin the
// inline-then-spill contract directly on the data structure, independent of a
// live scheduler — push far past the inline capacity, remove in every order,
// and assert nothing is lost and swap-with-last identity holds across the
// inline/spill boundary.

#include <cstdint>
#include <set>
#include <gtest/gtest.h>
#include <SushiRuntime/scheduler/event_polling.hpp>

using SushiRuntime::Scheduler::EventPolling;
using SushiRuntime::Scheduler::Task;

namespace
{
    /** @brief Encodes an integer id into the task pointer slot as an opaque tag. */
    Task* tag(std::uintptr_t id) { return reinterpret_cast<Task*>(id); }

    /** @brief Recovers the id previously encoded by tag(). */
    std::uintptr_t untag(Task* t) { return reinterpret_cast<std::uintptr_t>(t); }
}

/**
 * @brief Pushing past the inline capacity stores every event, none dropped.
 */
TEST(Unit_EventPolling, PushNeverDropsBeyondInlineCapacity)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 100000; // far beyond INLINE_CAP (256)

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push({sycl::event{}, tag(i), true});

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
TEST(Unit_EventPolling, RemoveAtDrainsEveryEventExactlyOnce)
{
    EventPolling::ActiveEventList list;
    constexpr std::uintptr_t N = 5000; // spans inline buffer and spill

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push({sycl::event{}, tag(i), false});

    std::set<std::uintptr_t> seen;
    // Always removing index 0 forces the tail to be relocated into the front
    // every step, exercising the inline/spill boundary repeatedly.
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
TEST(Unit_EventPolling, RemoveAcrossInlineSpillBoundary)
{
    EventPolling::ActiveEventList list;
    const std::uintptr_t N = EventPolling::ActiveEventList::INLINE_CAP + 1;

    for (std::uintptr_t i = 1; i <= N; ++i)
        list.push({sycl::event{}, tag(i), true});

    ASSERT_EQ(list.size(), N);

    // The only spilled entry is the last one (tag N). Removing an inline slot
    // must move that tail into the freed slot and shrink the spill to empty.
    list.remove_at(0);

    EXPECT_EQ(list.size(), N - 1);
    EXPECT_EQ(untag(list[0].task), N);
}

/**
 * @brief Out-of-range remove_at is a no-op, and removing the tail needs no move.
 */
TEST(Unit_EventPolling, RemoveAtEdgeCases)
{
    EventPolling::ActiveEventList list;
    list.push({sycl::event{}, tag(10), true});
    list.push({sycl::event{}, tag(20), true});

    list.remove_at(99); // out of range
    EXPECT_EQ(list.size(), 2u);

    list.remove_at(1); // remove the tail itself
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(untag(list[0].task), 10u);
}
