/**************************************************************************/
/* test_drop_counts.cpp                                                   */
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
 * @file test_drop_counts.cpp
 * @brief Tests DropCounts, which says how many dispatches a run left out of its timing.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/report/drop_counts.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default count lost nothing and names no node. */
TEST(Small_DropCounts, ADefaultCountLostNothing)
{
    const DropCounts counts;

    EXPECT_FALSE(counts.any());
    EXPECT_EQ(counts.total(), 0u);
    EXPECT_EQ(counts.first_node, DropCounts::K_NO_NODE);
    EXPECT_EQ(counts.of(DropCause::UnreadableEvent), 0u);
    EXPECT_EQ(counts.of(DropCause::FullRow), 0u);
    EXPECT_EQ(counts.of(DropCause::UnknownNode), 0u);
    EXPECT_EQ(counts.of(DropCause::NoEvent), 0u);
    EXPECT_EQ(counts.of(DropCause::NoQueue), 0u);
}

/** @brief Tests that any() says a dispatch was lost once one cause holds a count. */
TEST(Small_DropCounts, AnyTellsWhetherADispatchWasLost)
{
    DropCounts counts;
    counts.per_cause[static_cast<std::size_t>(DropCause::NoQueue)] = 5;

    EXPECT_TRUE(counts.any());
}

/** @brief Tests that every cause has its own slot, so no two causes share a count. */
TEST(Small_DropCounts, EveryCauseHasItsOwnSlot)
{
    DropCounts counts;
    counts.per_cause[static_cast<std::size_t>(DropCause::UnreadableEvent)] = 1;
    counts.per_cause[static_cast<std::size_t>(DropCause::FullRow)] = 2;
    counts.per_cause[static_cast<std::size_t>(DropCause::UnknownNode)] = 3;
    counts.per_cause[static_cast<std::size_t>(DropCause::NoEvent)] = 4;
    counts.per_cause[static_cast<std::size_t>(DropCause::NoQueue)] = 5;

    EXPECT_EQ(counts.of(DropCause::UnreadableEvent), 1u);
    EXPECT_EQ(counts.of(DropCause::FullRow), 2u);
    EXPECT_EQ(counts.of(DropCause::UnknownNode), 3u);
    EXPECT_EQ(counts.of(DropCause::NoEvent), 4u);
    EXPECT_EQ(counts.of(DropCause::NoQueue), 5u);
    EXPECT_EQ(counts.total(), 15u);
}
