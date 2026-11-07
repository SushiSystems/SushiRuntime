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
 * @file test_buffer_region.cpp
 * @brief Tests BufferRegion's overlap and containment queries, default regions included.
 * @see tests/foundation/region/README.md
 * @author Mustafa Garip
 */

#include <type_traits>
#include <vector>

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

    /** @brief Holds two regions and the answer a query about them is expected to give. */
    struct RegionPair
    {
        const char* label;    /**< Names the row in a failure. */
        BufferRegion subject; /**< The region the query is asked of. */
        BufferRegion other;   /**< The region the query is given. */
        bool expect;          /**< The expected answer. */
    };

    /** @brief Expects overlaps to give each row's answer. */
    void expect_overlaps(const std::vector<RegionPair>& rows)
    {
        for (const RegionPair& row : rows)
        {
            SCOPED_TRACE(row.label);
            EXPECT_EQ(row.subject.overlaps(row.other), row.expect);
        }
    }

    /** @brief Expects contains to give each row's answer. */
    void expect_contains(const std::vector<RegionPair>& rows)
    {
        for (const RegionPair& row : rows)
        {
            SCOPED_TRACE(row.label);
            EXPECT_EQ(row.subject.contains(row.other), row.expect);
        }
    }
}

/** @brief Tests that a default region is not valid and overlaps no region, itself included. */
TEST(Small_BufferRegion, ADefaultRegionOverlapsNothing)
{
    const BufferRegion empty;
    const BufferRegion whole(&first_allocation);

    EXPECT_FALSE(empty.valid());
    expect_overlaps({
        {"a default region with another", empty, BufferRegion(), false},
        {"a default region with itself", empty, empty, false},
        {"a default region with a whole allocation", empty, whole, false},
        {"a whole allocation with a default region", whole, empty, false},
        {"two regions with a null base", BufferRegion(nullptr, 0, 8), BufferRegion(nullptr, 4, 8), false},
    });
}

/** @brief Tests that a default region contains no region. */
TEST(Small_BufferRegion, ADefaultRegionContainsNothing)
{
    const BufferRegion empty;
    const BufferRegion whole(&first_allocation);

    expect_contains({
        {"a default region holds another", empty, BufferRegion(), false},
        {"a default region holds itself", empty, empty, false},
        {"a default region holds a whole allocation", empty, whole, false},
    });
}

/** @brief Tests that no region is contained by a default region. */
TEST(Small_BufferRegion, ADefaultRegionIsContainedByNothing)
{
    const BufferRegion empty;
    const BufferRegion whole(&first_allocation);

    expect_contains({{"a whole allocation holds a default region", whole, empty, false}});
}

/** @brief Tests that regions of one allocation overlap when their intervals intersect. */
TEST(Small_BufferRegion, RegionsOfOneAllocationOverlapWhenTheirIntervalsIntersect)
{
    const BufferRegion whole(&first_allocation);
    const BufferRegion low(&first_allocation, 0, 8);
    const BufferRegion high(&first_allocation, 8, 8);
    const BufferRegion middle(&first_allocation, 4, 8);
    const BufferRegion tail(&first_allocation, 12, BufferRegion::K_WHOLE);

    expect_overlaps({
        {"the whole with itself", whole, whole, true},
        {"the whole with a part", whole, low, true},
        {"a part with the whole", low, whole, true},
        {"overlapping parts", low, middle, true},
        {"overlapping parts, the other way", middle, high, true},
        {"a part with a tail", high, tail, true},
        {"a tail with a part", tail, high, true},
    });
}

/** @brief Tests that half-open intervals that only touch share no byte. */
TEST(Small_BufferRegion, TouchingRegionsShareNoByte)
{
    const BufferRegion low(&first_allocation, 0, 8);
    const BufferRegion high(&first_allocation, 8, 8);
    const BufferRegion tail(&first_allocation, 12, BufferRegion::K_WHOLE);

    expect_overlaps({
        {"[0, 8) and [8, 16)", low, high, false},
        {"[0, 8) and a tail from 12", low, tail, false},
        {"a tail from 12 and [0, 8)", tail, low, false},
    });
}

/** @brief Tests that regions of different allocations neither overlap nor contain each other. */
TEST(Small_BufferRegion, RegionsOfDifferentAllocationsNeverOverlap)
{
    const BufferRegion first(&first_allocation);
    const BufferRegion second(&second_allocation);

    EXPECT_FALSE(first.overlaps(second));
    EXPECT_FALSE(first.contains(second));
}

/** @brief Tests that a region contains what lies entirely inside it. */
TEST(Small_BufferRegion, ARegionContainsWhatLiesEntirelyInside)
{
    const BufferRegion whole(&first_allocation);
    const BufferRegion low(&first_allocation, 0, 8);
    const BufferRegion inner(&first_allocation, 2, 4);
    const BufferRegion middle(&first_allocation, 4, 8);

    expect_contains({
        {"the whole holds itself", whole, whole, true},
        {"the whole holds a part", whole, low, true},
        {"a part holds itself", low, low, true},
        {"a part holds an inner part", low, inner, true},
        {"a part does not hold the whole", low, whole, false},
        {"a part does not hold a part that sticks out", low, middle, false},
        {"an inner part does not hold the part around it", inner, low, false},
    });
}
