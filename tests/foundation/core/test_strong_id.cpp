/**************************************************************************/
/* test_strong_id.cpp                                                     */
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
 * @file test_strong_id.cpp
 * @brief Tests StrongID: construction, comparison, hashing and its type traits.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <functional>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/strong_id.hpp>

using SushiRuntime::StrongID;

namespace
{
    struct DeviceTag {};
    using DeviceID = StrongID<DeviceTag>;

    static_assert(std::is_trivially_copyable_v<DeviceID>,
                  "StrongID must be trivially copyable.");
    static_assert(sizeof(DeviceID) == sizeof(std::size_t),
                  "StrongID must have the size of its representation.");
    static_assert(!std::is_convertible_v<std::size_t, DeviceID>,
                  "A raw integer must not convert implicitly to StrongID.");

    /** @brief Returns the values of the keys of @p ordered, in map order. */
    std::vector<std::size_t> keys_of(const std::map<DeviceID, int>& ordered)
    {
        std::vector<std::size_t> keys;
        for (const auto& entry : ordered)
            keys.push_back(entry.first.value());
        return keys;
    }

    TEST(Small_StrongId, ExplicitConstructionKeepsValue)
    {
        const DeviceID id{7};
        EXPECT_EQ(id.value(), std::size_t{7});
    }

    TEST(Small_StrongId, EqualityComparesValues)
    {
        EXPECT_TRUE(DeviceID{3} == DeviceID{3});
        EXPECT_FALSE(DeviceID{3} != DeviceID{3});
        EXPECT_TRUE(DeviceID{3} != DeviceID{4});
        EXPECT_FALSE(DeviceID{3} == DeviceID{4});
    }

    TEST(Small_StrongId, OrderingFollowsValues)
    {
        EXPECT_TRUE(DeviceID{1} < DeviceID{2});
        EXPECT_FALSE(DeviceID{2} < DeviceID{2});
        EXPECT_FALSE(DeviceID{3} < DeviceID{2});

        std::map<DeviceID, int> ordered;
        ordered[DeviceID{9}] = 90;
        ordered[DeviceID{2}] = 20;
        ordered[DeviceID{5}] = 50;

        const std::vector<std::size_t> keys = keys_of(ordered);
        EXPECT_EQ(keys, (std::vector<std::size_t>{2, 5, 9}));
    }

    TEST(Small_StrongId, UsableAsUnorderedMapKey)
    {
        std::unordered_map<DeviceID, int> names;
        names[DeviceID{5}] = 50;
        names[DeviceID{9}] = 90;
        ASSERT_EQ(names.size(), 2u);
        EXPECT_EQ(names.at(DeviceID{5}), 50);
        EXPECT_EQ(names.count(DeviceID{0}), 0u);
    }

    TEST(Small_StrongId, HashForwardsToRepresentation)
    {
        EXPECT_EQ(std::hash<DeviceID>{}(DeviceID{42}), std::hash<std::size_t>{}(42));
    }
}
