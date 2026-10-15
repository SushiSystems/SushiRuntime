/**************************************************************************/
/* test_buffer_region.cpp                                                 */
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
 * @file test_buffer_region.cpp
 * @brief Tests BufferRegion's overlap and containment queries, default regions included.
 * @see tests/foundation/region/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/region/buffer_region.hpp>

using SushiRuntime::BufferRegion;

static_assert(!std::is_convertible<void*, BufferRegion>::value,
              "a base pointer becomes a region only where the code spells BufferRegion");

namespace
{
    /** @brief Stands in for one allocation; only its address is used. */
    int first_allocation = 0;

    /** @brief Stands in for a second allocation; only its address is used. */
    int second_allocation = 0;
}

TEST(Small_BufferRegion, ADefaultRegionOverlapsNothing)
{
    const BufferRegion empty;
    const BufferRegion other_empty;
    const BufferRegion whole(&first_allocation);

    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(empty.overlaps(other_empty));
    EXPECT_FALSE(empty.overlaps(empty));
    EXPECT_FALSE(empty.overlaps(whole));
    EXPECT_FALSE(whole.overlaps(empty));
    EXPECT_FALSE(BufferRegion(nullptr, 0, 8).overlaps(BufferRegion(nullptr, 4, 8)));
}

TEST(Small_BufferRegion, ADefaultRegionContainsNothingAndIsContainedByNothing)
{
    const BufferRegion empty;
    const BufferRegion other_empty;
    const BufferRegion whole(&first_allocation);

    EXPECT_FALSE(empty.contains(other_empty));
    EXPECT_FALSE(empty.contains(empty));
    EXPECT_FALSE(empty.contains(whole));
    EXPECT_FALSE(whole.contains(empty));
}

TEST(Small_BufferRegion, RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect)
{
    const BufferRegion whole(&first_allocation);
    const BufferRegion low(&first_allocation, 0, 8);
    const BufferRegion high(&first_allocation, 8, 8);
    const BufferRegion middle(&first_allocation, 4, 8);
    const BufferRegion tail(&first_allocation, 12, BufferRegion::K_WHOLE);

    EXPECT_TRUE(whole.overlaps(whole));
    EXPECT_TRUE(whole.overlaps(low));
    EXPECT_TRUE(low.overlaps(whole));
    EXPECT_TRUE(low.overlaps(middle));
    EXPECT_TRUE(middle.overlaps(high));
    EXPECT_TRUE(high.overlaps(tail));
    EXPECT_TRUE(tail.overlaps(high));

    // The intervals are half-open: [0, 8) and [8, 16) share no byte.
    EXPECT_FALSE(low.overlaps(high));
    EXPECT_FALSE(low.overlaps(tail));
    EXPECT_FALSE(tail.overlaps(low));
}

TEST(Small_BufferRegion, RegionsOfDifferentAllocationsNeverOverlap)
{
    const BufferRegion first(&first_allocation);
    const BufferRegion second(&second_allocation);

    EXPECT_FALSE(first.overlaps(second));
    EXPECT_FALSE(first.contains(second));
}

TEST(Small_BufferRegion, ARegionContainsWhatLiesEntirelyInside)
{
    const BufferRegion whole(&first_allocation);
    const BufferRegion low(&first_allocation, 0, 8);
    const BufferRegion inner(&first_allocation, 2, 4);
    const BufferRegion middle(&first_allocation, 4, 8);

    EXPECT_TRUE(whole.contains(whole));
    EXPECT_TRUE(whole.contains(low));
    EXPECT_TRUE(low.contains(low));
    EXPECT_TRUE(low.contains(inner));

    EXPECT_FALSE(low.contains(whole));
    EXPECT_FALSE(low.contains(middle));
    EXPECT_FALSE(inner.contains(low));
}
