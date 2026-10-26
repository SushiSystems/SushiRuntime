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
 * @file test_drop_counts.cpp
 * @brief Tests DropCounts, which says how many dispatches a run left out of its timing.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/report/drop_counts.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default count holds nothing and names no node. */
TEST(Small_DropCounts, ADefaultCountLostNothing)
{
    const DropCounts counts;

    EXPECT_FALSE(counts.any());
    EXPECT_EQ(counts.total(), 0u);
    EXPECT_EQ(counts.first_node, DropCounts::K_NO_NODE);
    for (std::size_t cause = 0; cause < DropCounts::K_CAUSES; ++cause)
        EXPECT_EQ(counts.per_cause[cause], 0u);
}

/** @brief Tests that a count is kept per cause and that the total and any() read them all. */
TEST(Small_DropCounts, CountsPerCauseAndSaysWhetherAnyWasLost)
{
    DropCounts counts;
    counts.per_cause[static_cast<std::size_t>(DropCause::FullRow)] = 2;
    counts.per_cause[static_cast<std::size_t>(DropCause::NoQueue)] = 5;
    counts.first_node = 4;

    EXPECT_TRUE(counts.any());
    EXPECT_EQ(counts.of(DropCause::FullRow), 2u);
    EXPECT_EQ(counts.of(DropCause::NoQueue), 5u);
    EXPECT_EQ(counts.of(DropCause::UnreadableEvent), 0u);
    EXPECT_EQ(counts.of(DropCause::UnknownNode), 0u);
    EXPECT_EQ(counts.of(DropCause::NoEvent), 0u);
    EXPECT_EQ(counts.total(), 7u);
}

/** @brief Tests that every cause has its own slot, so no two causes share a count. */
TEST(Small_DropCounts, EveryCauseHasItsOwnSlot)
{
    DropCounts counts;
    const DropCause causes[] = {DropCause::UnreadableEvent, DropCause::FullRow, DropCause::UnknownNode,
                                DropCause::NoEvent, DropCause::NoQueue};
    std::uint32_t next = 1;
    for (const DropCause cause : causes)
        counts.per_cause[static_cast<std::size_t>(cause)] = next++;

    EXPECT_EQ(counts.of(DropCause::UnreadableEvent), 1u);
    EXPECT_EQ(counts.of(DropCause::FullRow), 2u);
    EXPECT_EQ(counts.of(DropCause::UnknownNode), 3u);
    EXPECT_EQ(counts.of(DropCause::NoEvent), 4u);
    EXPECT_EQ(counts.of(DropCause::NoQueue), 5u);
    EXPECT_EQ(counts.total(), 15u);
}
