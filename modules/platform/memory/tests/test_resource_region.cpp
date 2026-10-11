/**************************************************************************/
/* test_resource_region.cpp                                               */
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
 * @file test_resource_region.cpp
 * @brief Tests ResourceRegion's overlap and containment queries, default regions included.
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/resource_region.hpp>

using SushiRuntime::Memory::ResourceRegion;

namespace
{
    /** @brief Stands in for one allocation; only its address is used. */
    int first_allocation = 0;

    /** @brief Stands in for a second allocation; only its address is used. */
    int second_allocation = 0;
}

TEST(Unit_ResourceRegion, ADefaultRegionOverlapsNothing)
{
    const ResourceRegion empty;
    const ResourceRegion other_empty;
    const ResourceRegion whole(&first_allocation);

    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(empty.overlaps(other_empty));
    EXPECT_FALSE(empty.overlaps(empty));
    EXPECT_FALSE(empty.overlaps(whole));
    EXPECT_FALSE(whole.overlaps(empty));
    EXPECT_FALSE(ResourceRegion(nullptr, 0, 8).overlaps(ResourceRegion(nullptr, 4, 8)));
}

TEST(Unit_ResourceRegion, ADefaultRegionContainsNothingAndIsContainedByNothing)
{
    const ResourceRegion empty;
    const ResourceRegion other_empty;
    const ResourceRegion whole(&first_allocation);

    EXPECT_FALSE(empty.contains(other_empty));
    EXPECT_FALSE(empty.contains(empty));
    EXPECT_FALSE(empty.contains(whole));
    EXPECT_FALSE(whole.contains(empty));
}

TEST(Unit_ResourceRegion, RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect)
{
    const ResourceRegion whole(&first_allocation);
    const ResourceRegion low(&first_allocation, 0, 8);
    const ResourceRegion high(&first_allocation, 8, 8);
    const ResourceRegion middle(&first_allocation, 4, 8);
    const ResourceRegion tail(&first_allocation, 12, ResourceRegion::WHOLE);

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

TEST(Unit_ResourceRegion, RegionsOfDifferentAllocationsNeverOverlap)
{
    const ResourceRegion first(&first_allocation);
    const ResourceRegion second(&second_allocation);

    EXPECT_FALSE(first.overlaps(second));
    EXPECT_FALSE(first.contains(second));
}

TEST(Unit_ResourceRegion, ARegionContainsWhatLiesEntirelyInside)
{
    const ResourceRegion whole(&first_allocation);
    const ResourceRegion low(&first_allocation, 0, 8);
    const ResourceRegion inner(&first_allocation, 2, 4);
    const ResourceRegion middle(&first_allocation, 4, 8);

    EXPECT_TRUE(whole.contains(whole));
    EXPECT_TRUE(whole.contains(low));
    EXPECT_TRUE(low.contains(low));
    EXPECT_TRUE(low.contains(inner));

    EXPECT_FALSE(low.contains(whole));
    EXPECT_FALSE(low.contains(middle));
    EXPECT_FALSE(inner.contains(low));
}
