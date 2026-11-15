/**************************************************************************/
/* test_memory_space_id.cpp                                               */
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
 * @file test_memory_space_id.cpp
 * @brief Tests MemorySpaceID's value, comparisons and hash.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/memory_space_id.hpp>

using SushiRuntime::MemorySpaceID;

static_assert(!std::is_convertible<std::uint32_t, MemorySpaceID>::value,
              "an integer becomes a MemorySpaceID only where the code spells the type");

TEST(Small_MemorySpaceID, KeepsItsValueAndDefaultsToZero)
{
    EXPECT_EQ(MemorySpaceID().value(), 0U);
    EXPECT_EQ(MemorySpaceID(UINT32_MAX).value(), UINT32_MAX);
}

TEST(Small_MemorySpaceID, ComparesByValue)
{
    EXPECT_EQ(MemorySpaceID(3), MemorySpaceID(3));
    EXPECT_NE(MemorySpaceID(3), MemorySpaceID(4));
    EXPECT_TRUE(MemorySpaceID(3) < MemorySpaceID(4));
    EXPECT_FALSE(MemorySpaceID(4) < MemorySpaceID(3));
    EXPECT_FALSE(MemorySpaceID(3) < MemorySpaceID(3));
}

TEST(Small_MemorySpaceID, EqualIDsHashEqualAndDistinctIDsApart)
{
    const std::hash<MemorySpaceID> hash;
    EXPECT_EQ(hash(MemorySpaceID(9)), hash(MemorySpaceID(9)));
    std::unordered_set<std::size_t> seen;
    for (std::uint32_t value = 0; value < 64; ++value)
        seen.insert(hash(MemorySpaceID(value)));
    EXPECT_EQ(seen.size(), 64U);
}
