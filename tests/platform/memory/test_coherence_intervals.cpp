/**************************************************************************/
/* test_coherence_intervals.cpp                                           */
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
 * @file test_coherence_intervals.cpp
 * @brief Tests splitting, merging and invalidation in the coherence interval map.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/coherence_intervals.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    const MemorySpaceID K_HOST(0);
    const MemorySpaceID K_DEVICE(1);
    const MemorySpaceID K_OTHER_DEVICE(2);

    /** @brief Returns the bitmask naming space alone. */
    std::uint64_t bit(MemorySpaceID space)
    {
        return std::uint64_t{1} << space.value();
    }

    TEST(Small_CoherenceIntervals, ResetMakesOneIntervalOnHome)
    {
        CoherenceIntervals map;
        map.reset(100, K_HOST);
        ASSERT_EQ(map.intervals().size(), 1U);
        EXPECT_EQ(map.intervals()[0].begin, 0U);
        EXPECT_EQ(map.intervals()[0].end, 100U);
        EXPECT_EQ(map.intervals()[0].valid_spaces, bit(K_HOST));
        EXPECT_EQ(map.size(), 100U);
    }

    TEST(Small_CoherenceIntervals, PartialWriteSplitsIntoThree)
    {
        CoherenceIntervals map;
        map.reset(100, K_HOST);
        const std::uint64_t version = map.record_write(20, 60, K_DEVICE);
        EXPECT_NE(version, 0U);

        ASSERT_EQ(map.intervals().size(), 3U);
        EXPECT_EQ(map.intervals()[0].end, 20U);
        EXPECT_EQ(map.intervals()[1].begin, 20U);
        EXPECT_EQ(map.intervals()[1].end, 60U);
        EXPECT_EQ(map.intervals()[1].version, version);
        EXPECT_EQ(map.intervals()[2].begin, 60U);
        EXPECT_EQ(map.intervals()[2].end, 100U);
        EXPECT_EQ(map.valid_spaces_at(10), bit(K_HOST));
        EXPECT_EQ(map.valid_spaces_at(59), bit(K_DEVICE));
        EXPECT_EQ(map.valid_spaces_at(60), bit(K_HOST));
    }

    TEST(Small_CoherenceIntervals, WriteInvalidatesEveryOtherSpace)
    {
        CoherenceIntervals map;
        map.reset(64, K_HOST);
        map.record_copy(0, 64, K_DEVICE);
        map.record_copy(0, 64, K_OTHER_DEVICE);
        EXPECT_EQ(map.valid_spaces_at(0), bit(K_HOST) | bit(K_DEVICE) | bit(K_OTHER_DEVICE));

        map.record_write(0, 32, K_DEVICE);
        EXPECT_EQ(map.valid_spaces_at(0), bit(K_DEVICE));
        EXPECT_EQ(map.valid_spaces_at(32), bit(K_HOST) | bit(K_DEVICE) | bit(K_OTHER_DEVICE));
    }

    TEST(Small_CoherenceIntervals, EqualNeighboursMerge)
    {
        CoherenceIntervals map;
        map.reset(100, K_HOST);
        map.record_copy(0, 50, K_DEVICE);
        ASSERT_EQ(map.intervals().size(), 2U);

        map.record_copy(50, 100, K_DEVICE);
        ASSERT_EQ(map.intervals().size(), 1U);
        EXPECT_EQ(map.intervals()[0].valid_spaces, bit(K_HOST) | bit(K_DEVICE));
    }

    TEST(Small_CoherenceIntervals, WholeBufferWriteStaysOneInterval)
    {
        CoherenceIntervals map;
        map.reset(100, K_HOST);
        map.record_write(10, 30, K_DEVICE);
        map.record_write(50, 70, K_OTHER_DEVICE);
        ASSERT_EQ(map.intervals().size(), 5U);

        const std::uint64_t version = map.record_write(0, 100, K_DEVICE);
        ASSERT_EQ(map.intervals().size(), 1U);
        EXPECT_EQ(map.intervals()[0].valid_spaces, bit(K_DEVICE));
        EXPECT_EQ(map.intervals()[0].version, version);

        map.record_write(0, 100, K_HOST);
        EXPECT_EQ(map.intervals().size(), 1U);
    }

    TEST(Small_CoherenceIntervals, OutOfRangeWriteChangesNothing)
    {
        CoherenceIntervals map;
        map.reset(16, K_HOST);
        EXPECT_EQ(map.record_write(16, 32, K_DEVICE), 0U);
        EXPECT_EQ(map.record_write(4, 4, K_DEVICE), 0U);
        EXPECT_EQ(map.record_write(0, 8, MemorySpaceID(CoherenceIntervals::K_MAX_MEMORY_SPACES)), 0U);
        EXPECT_EQ(map.intervals().size(), 1U);
        EXPECT_EQ(map.record_write(8, 1000, K_DEVICE) != 0U, true);
        EXPECT_EQ(map.size(), 16U);
    }

    TEST(Small_CoherenceIntervals, DroppingASpaceRemovesItFromEveryIntervalAndJoinsEqualNeighbours)
    {
        CoherenceIntervals map;
        map.reset(16, K_HOST);
        map.record_copy(0, 8, K_DEVICE);
        ASSERT_EQ(map.intervals().size(), 2U);

        map.drop_space(K_DEVICE);
        ASSERT_EQ(map.intervals().size(), 1U);
        EXPECT_EQ(map.intervals()[0].valid_spaces, bit(K_HOST));
        EXPECT_EQ(map.intervals()[0].end, 16U);

        map.drop_space(MemorySpaceID(CoherenceIntervals::K_MAX_MEMORY_SPACES));
        EXPECT_EQ(map.intervals().size(), 1U);
    }

    TEST(Small_CoherenceIntervals, ClearEmptiesTheMap)
    {
        CoherenceIntervals map;
        map.reset(16, K_HOST);
        map.clear();
        EXPECT_TRUE(map.intervals().empty());
        EXPECT_EQ(map.valid_spaces_at(0), 0U);
    }
} // namespace
