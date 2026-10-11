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
 * @file test_small_event_vector.cpp
 * @brief Tests the copy, move, assignment and clear behaviour of SmallEventVector.
 * @see modules/engine/task/tests/README.md
 * @author Mustafa Garip
 */

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
} // namespace

TEST(Unit_SmallEventVector, ADefaultVectorIsEmpty)
{
    const SmallEventVector events;

    EXPECT_EQ(events.size(), 0u);
    EXPECT_TRUE(events.to_vector().empty());
}

TEST(Unit_SmallEventVector, ACopyHoldsTheSameEventsInlineAndOverflow)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector source;
    source = expected;

    const SmallEventVector copy(source);

    EXPECT_EQ(copy.size(), expected.size());
    EXPECT_EQ(copy.to_vector(), expected);
    EXPECT_EQ(source.to_vector(), expected);
}

TEST(Unit_SmallEventVector, AMoveHoldsTheEventsAndEmptiesTheSource)
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

TEST(Unit_SmallEventVector, ACopyAssignmentReplacesEveryPriorEvent)
{
    const std::vector<sycl::event> expected = distinct_events(3);
    SmallEventVector source;
    source = expected;

    SmallEventVector target;
    target = distinct_events(10);
    target = source;

    EXPECT_EQ(target.size(), expected.size());
    EXPECT_EQ(target.to_vector(), expected);
    EXPECT_TRUE(target.dynamic_data.empty());
    EXPECT_EQ(source.to_vector(), expected);
}

TEST(Unit_SmallEventVector, AMoveAssignmentReplacesEveryPriorEventAndEmptiesTheSource)
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

TEST(Unit_SmallEventVector, AssignmentFromAVectorShrinksPastTheOldContent)
{
    SmallEventVector events;
    events = distinct_events(10);

    const std::vector<sycl::event> expected = distinct_events(3);
    events = expected;

    EXPECT_EQ(events.size(), expected.size());
    EXPECT_EQ(events.to_vector(), expected);
    EXPECT_TRUE(events.dynamic_data.empty());
}

TEST(Unit_SmallEventVector, AssignmentPastTheInlineCapacityKeepsEveryElementInOrder)
{
    const std::vector<sycl::event> expected = distinct_events(20);
    SmallEventVector events;
    events = expected;

    EXPECT_EQ(events.size(), 20u);
    EXPECT_EQ(events.dynamic_data.size(), 12u);
    EXPECT_EQ(events.to_vector(), expected);

    std::size_t index = 0;
    for (const sycl::event& stored : events)
    {
        ASSERT_LT(index, expected.size());
        EXPECT_EQ(stored, expected[index]);
        ++index;
    }
    EXPECT_EQ(index, expected.size());
}

TEST(Unit_SmallEventVector, ClearEmptiesTheVectorAndItAcceptsNewEvents)
{
    SmallEventVector events;
    events = distinct_events(10);

    events.clear();

    EXPECT_EQ(events.size(), 0u);
    EXPECT_TRUE(events.to_vector().empty());

    const std::vector<sycl::event> expected = distinct_events(2);
    events = expected;
    EXPECT_EQ(events.to_vector(), expected);
}

TEST(Unit_SmallEventVector, SelfAssignmentKeepsEveryEvent)
{
    const std::vector<sycl::event> expected = distinct_events(10);
    SmallEventVector events;
    events = expected;
    SmallEventVector& same = events;

    events = same;
    EXPECT_EQ(events.to_vector(), expected);

    events = std::move(same);
    EXPECT_EQ(events.to_vector(), expected);
}

TEST(Unit_SmallEventVector, AssignmentFromItsOwnOverflowKeepsThatList)
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
