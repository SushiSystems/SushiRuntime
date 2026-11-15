/**************************************************************************/
/* test_split_sizing.cpp                                                  */
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
 * @file test_split_sizing.cpp
 * @brief Tests size_split(): parts sized by device speed, at most one per device, covering the axis.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/split_sizing.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the summed length of parts, and checks that they are contiguous and in slice order. */
    std::uint64_t covered(const std::vector<SizedPart>& parts)
    {
        std::uint64_t next = 0;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            EXPECT_EQ(parts[index].part.begin, next);
            EXPECT_EQ(parts[index].part.index, index);
            EXPECT_EQ(parts[index].part.count, parts.size());
            next = parts[index].part.end;
        }
        return next;
    }

    /** @brief Tests that the parts are sized by speed and sum to the extent. */
    TEST(Small_SplitSizing, PartsSizedBySpeedSumToTheExtent)
    {
        const std::vector<SizedPart> parts = size_split(2, 90, {1.0, 0.5});

        ASSERT_EQ(parts.size(), 2u);
        EXPECT_EQ(parts[0].device, 0u);
        EXPECT_EQ(parts[0].part.end - parts[0].part.begin, 30u);
        EXPECT_EQ(parts[1].device, 1u);
        EXPECT_EQ(parts[1].part.end - parts[1].part.begin, 60u);
        EXPECT_EQ(parts[0].part.axis, 2u);
        EXPECT_EQ(covered(parts), 90u);
    }

    /** @brief Tests that the remainder of the rounding goes to the fastest device, the first of equals. */
    TEST(Small_SplitSizing, TheRemainderGoesToTheFastestDevice)
    {
        const std::vector<SizedPart> parts = size_split(0, 10, {1.0, 1.0, 1.0});

        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0].part.end - parts[0].part.begin, 4u);
        EXPECT_EQ(parts[1].part.end - parts[1].part.begin, 3u);
        EXPECT_EQ(parts[2].part.end - parts[2].part.begin, 3u);
        EXPECT_EQ(covered(parts), 10u);
    }

    /** @brief Tests that no device gets more than one part and one that cannot run the node gets none. */
    TEST(Small_SplitSizing, OnePartPerDeviceAtMost)
    {
        const double never = std::numeric_limits<double>::infinity();
        const std::vector<SizedPart> parts = size_split(0, 1000, {2.0, never, 1.0, 4.0});

        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[0].device, 0u);
        EXPECT_EQ(parts[1].device, 2u);
        EXPECT_EQ(parts[2].device, 3u);
        EXPECT_EQ(covered(parts), 1000u);
    }

    /** @brief Tests that an axis shorter than the device count leaves the slow devices without a part. */
    TEST(Small_SplitSizing, AShortAxisGivesFewerParts)
    {
        const std::vector<SizedPart> parts = size_split(0, 1, {1.0, 2.0});

        ASSERT_EQ(parts.size(), 1u);
        EXPECT_EQ(parts[0].device, 0u);
        EXPECT_EQ(covered(parts), 1u);
    }

    /** @brief Tests that equal inputs give equal parts. */
    TEST(Small_SplitSizing, EqualInputsGiveEqualParts)
    {
        const std::vector<SizedPart> first = size_split(1, 12345, {0.3, 0.7, 0.11});
        const std::vector<SizedPart> second = size_split(1, 12345, {0.3, 0.7, 0.11});

        ASSERT_EQ(first.size(), second.size());
        for (std::size_t index = 0; index < first.size(); ++index)
        {
            EXPECT_EQ(first[index].device, second[index].device);
            EXPECT_EQ(first[index].part, second[index].part);
        }
    }

    /** @brief Tests that a zero extent or no usable device gives no part. */
    TEST(Small_SplitSizing, NothingToCutGivesNoPart)
    {
        EXPECT_TRUE(size_split(0, 0, {1.0}).empty());
        EXPECT_TRUE(size_split(0, 10, {std::numeric_limits<double>::infinity()}).empty());
        EXPECT_TRUE(size_split(0, 10, {}).empty());
    }
} // namespace
