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
 * @file test_strong_id.cpp
 * @brief Tests StrongID: defaults, construction, comparison, hashing and its type traits.
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

    TEST(Unit_StrongId, DefaultIsZero)
    {
        EXPECT_EQ(DeviceID{}.value(), std::size_t{0});
    }

    TEST(Unit_StrongId, ExplicitConstructionKeepsValue)
    {
        const DeviceID id{7};
        EXPECT_EQ(id.value(), std::size_t{7});
    }

    TEST(Unit_StrongId, EqualityComparesValues)
    {
        EXPECT_TRUE(DeviceID{3} == DeviceID{3});
        EXPECT_FALSE(DeviceID{3} != DeviceID{3});
        EXPECT_TRUE(DeviceID{3} != DeviceID{4});
        EXPECT_FALSE(DeviceID{3} == DeviceID{4});
    }

    TEST(Unit_StrongId, OrderingFollowsValues)
    {
        EXPECT_TRUE(DeviceID{1} < DeviceID{2});
        EXPECT_FALSE(DeviceID{2} < DeviceID{2});
        EXPECT_FALSE(DeviceID{3} < DeviceID{2});
    }

    TEST(Unit_StrongId, UsableAsUnorderedMapKey)
    {
        std::unordered_map<DeviceID, int> names;
        names[DeviceID{5}] = 50;
        names[DeviceID{9}] = 90;
        ASSERT_EQ(names.size(), 2u);
        EXPECT_EQ(names.at(DeviceID{5}), 50);
        EXPECT_EQ(names.count(DeviceID{0}), 0u);
    }

    TEST(Unit_StrongId, UsableAsOrderedMapKey)
    {
        std::map<DeviceID, int> ordered;
        ordered[DeviceID{9}] = 90;
        ordered[DeviceID{2}] = 20;
        ordered[DeviceID{5}] = 50;

        std::vector<std::size_t> keys;
        for (const auto& entry : ordered)
            keys.push_back(entry.first.value());
        EXPECT_EQ(keys, (std::vector<std::size_t>{2, 5, 9}));
    }

    TEST(Unit_StrongId, HashForwardsToRepresentation)
    {
        EXPECT_EQ(std::hash<DeviceID>{}(DeviceID{42}), std::hash<std::size_t>{}(42));
    }
}
