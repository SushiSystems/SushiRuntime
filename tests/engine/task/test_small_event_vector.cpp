/**************************************************************************/
/* test_small_event_vector.cpp                                            */
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
 * @file test_small_event_vector.cpp
 * @brief Tests the copy, move, assignment and clear behaviour of SmallEventVector.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/task/small_event_vector.hpp>

namespace
{
    using SushiRuntime::Internal::Graph::SmallEventVector;

    /**
     * @brief Returns @p count default-constructed events, each one a distinct event.
     * @param count Number of events to return.
     * @return The events, in order.
     */
    std::vector<sycl::event> distinct_events(std::size_t count)
    {
        return std::vector<sycl::event>(count);
    }

    /** @brief Builds a vector that holds @p count events. */
    SmallEventVector holding(std::size_t count)
    {
        SmallEventVector events;
        events = distinct_events(count);
        return events;
    }

    /** @brief Returns the events @p events visits when iterated, in order. */
    std::vector<sycl::event> visited(const SmallEventVector& events)
    {
        std::vector<sycl::event> seen;
        for (const sycl::event& stored : events)
            seen.push_back(stored);
        return seen;
    }

    /** @brief Returns how many of @p count events do not fit the inline storage. */
    std::size_t overflow_of(std::size_t count)
    {
        return std::max(count, SmallEventVector::K_STATIC_CAP) - SmallEventVector::K_STATIC_CAP;
    }

    /** @brief One assignment: the events the target held before, and the events assigned to it. */
    struct AssignmentRow
    {
        /** @brief Events the target holds before the assignment. */
        std::size_t prior;
        /** @brief Events the source holds. */
        std::size_t assigned;
    };
} // namespace

/** @brief Fixture of the rows of the assignment that replaces prior events. */
class Small_SmallEventVectorAssignment : public ::testing::TestWithParam<AssignmentRow>
{
};

TEST(Small_SmallEventVector, ACopyHoldsTheSameEventsInlineAndOverflow)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector source;
    source = expected;

    const SmallEventVector copy(source);

    EXPECT_EQ(copy.size(), expected.size());
    EXPECT_EQ(copy.to_vector(), expected);
    EXPECT_EQ(source.to_vector(), expected);
}

TEST(Small_SmallEventVector, AMoveHoldsTheEventsAndEmptiesTheSource)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector source;
    source = expected;

    const SmallEventVector moved(std::move(source));

    EXPECT_EQ(moved.size(), expected.size());
    EXPECT_EQ(moved.to_vector(), expected);
    EXPECT_EQ(source.size(), 0u);
    EXPECT_TRUE(source.to_vector().empty());
    EXPECT_TRUE(source.dynamic_data.empty());
}

/** @brief Tests that assigning a SmallEventVector, or a std::vector, replaces every prior event. */
TEST_P(Small_SmallEventVectorAssignment, AssignmentReplacesEveryPriorEvent)
{
    const AssignmentRow row = GetParam();
    const std::vector<sycl::event> expected = distinct_events(row.assigned);
    SmallEventVector source;
    source = expected;
    SmallEventVector from_small = holding(row.prior);
    SmallEventVector from_vector = holding(row.prior);

    from_small = source;
    from_vector = expected;

    EXPECT_EQ(from_small.to_vector(), expected);
    EXPECT_EQ(from_vector.to_vector(), expected);
    EXPECT_EQ(from_small.dynamic_data.size(), overflow_of(row.assigned));
    EXPECT_EQ(from_vector.dynamic_data.size(), overflow_of(row.assigned));
    EXPECT_EQ(source.to_vector(), expected);
}

INSTANTIATE_TEST_SUITE_P(PriorAndAssigned, Small_SmallEventVectorAssignment,
                         ::testing::Values(AssignmentRow{10, 3}, AssignmentRow{3, 10}, AssignmentRow{0, 5},
                                           AssignmentRow{10, 0}, AssignmentRow{10, 20}));

TEST(Small_SmallEventVector, AMoveAssignmentReplacesEveryPriorEventAndEmptiesTheSource)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector source;
    source = expected;

    SmallEventVector target;
    target = distinct_events(3);
    target = std::move(source);

    EXPECT_EQ(target.size(), expected.size());
    EXPECT_EQ(target.to_vector(), expected);
    EXPECT_EQ(source.size(), 0u);
    EXPECT_TRUE(source.to_vector().empty());
}

/** @brief Tests that assigning past the inline capacity keeps every element, in order. */
TEST(Small_SmallEventVector, AssignmentPastTheInlineCapacityKeepsEveryElementInOrder)
{
    const std::vector<sycl::event> expected = distinct_events(20);
    SmallEventVector events;
    events = expected;

    EXPECT_EQ(events.size(), 20u);
    EXPECT_EQ(events.dynamic_data.size(), 12u);
    EXPECT_EQ(events.to_vector(), expected);
}

/** @brief Tests that iterating a vector past the inline capacity visits every event, in order. */
TEST(Small_SmallEventVector, IterationVisitsEveryEventInOrder)
{
    const std::vector<sycl::event> expected = distinct_events(20);
    SmallEventVector events;
    events = expected;

    EXPECT_EQ(visited(events), expected);
}

/** @brief Tests that clear() empties the vector. */
TEST(Small_SmallEventVector, ClearEmptiesTheVector)
{
    SmallEventVector events = holding(10);

    events.clear();

    EXPECT_EQ(events.size(), 0u);
    EXPECT_TRUE(events.to_vector().empty());
}

/** @brief Tests that a cleared vector accepts new events. */
TEST(Small_SmallEventVector, AClearedVectorAcceptsNewEvents)
{
    SmallEventVector events = holding(10);
    events.clear();
    const std::vector<sycl::event> expected = distinct_events(2);

    events = expected;

    EXPECT_EQ(events.to_vector(), expected);
}

/** @brief Tests that a copy assignment to itself keeps every event. */
TEST(Small_SmallEventVector, SelfCopyAssignmentKeepsEveryEvent)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector events;
    events = expected;
    SmallEventVector& same = events;

    events = same;

    EXPECT_EQ(events.to_vector(), expected);
}

/** @brief Tests that a move assignment to itself keeps every event. */
TEST(Small_SmallEventVector, SelfMoveAssignmentKeepsEveryEvent)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector events;
    events = expected;
    SmallEventVector& same = events;

    events = std::move(same);

    EXPECT_EQ(events.to_vector(), expected);
}

TEST(Small_SmallEventVector, AssignmentFromItsOwnOverflowKeepsThatList)
{
    const std::vector<sycl::event> all = distinct_events(20);
    SmallEventVector events;
    events = all;
    const std::vector<sycl::event> overflow(all.begin() + 8, all.end());

    events = events.dynamic_data;

    EXPECT_EQ(events.size(), overflow.size());
    EXPECT_EQ(events.to_vector(), overflow);
    EXPECT_EQ(events.dynamic_data.size(), overflow.size() - SmallEventVector::K_STATIC_CAP);
}
