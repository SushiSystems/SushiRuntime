/**************************************************************************/
/* test_placement_class.cpp                                               */
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
 * @file test_placement_class.cpp
 * @brief Tests PlacementClass: the whole-name hash, the prefix hashes, the literal and naming.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Literals;

static_assert(std::is_trivially_copyable_v<PlacementClass>, "a class stores no name");
static_assert("sushiengine.physics"_class.is_named(), "the literal is evaluated at compile time");

/** @brief Tests that the value is the OperationID hash of the whole name. */
TEST(Small_PlacementClass, ValueIsTheHashOfTheWholeName)
{
    constexpr PlacementClass named = "sushiengine.matter.water"_class;
    EXPECT_EQ(named.value, OperationID::hash("sushiengine.matter.water", 24));
    EXPECT_TRUE(named.is_named());
}

/** @brief Tests the prefixes of a three-segment name. */
TEST(Small_PlacementClass, ThreeSegmentNameHoldsTwoPrefixes)
{
    constexpr PlacementClass named = "sushiengine.matter.water"_class;
    ASSERT_EQ(named.prefix_count, 2u);
    EXPECT_EQ(named.prefixes[0], OperationID::hash("sushiengine", 11));
    EXPECT_EQ(named.prefixes[1], OperationID::hash("sushiengine.matter", 18));
}

/** @brief Tests that a one-segment name holds no prefix. */
TEST(Small_PlacementClass, OneSegmentNameHoldsNoPrefix)
{
    constexpr PlacementClass named = "sushiai"_class;
    EXPECT_EQ(named.prefix_count, 0u);
    EXPECT_FALSE(named.has_prefix(OperationID::hash("sushiai", 7)));
}

/** @brief Tests that a six-segment name keeps only the first four prefixes. */
TEST(Small_PlacementClass, SixSegmentNameKeepsFourPrefixes)
{
    constexpr PlacementClass named = "a.b.c.d.e.f"_class;
    ASSERT_EQ(named.prefix_count, PlacementClass::K_MAX_PREFIX_DEPTH);
    EXPECT_EQ(named.prefixes[3], OperationID::hash("a.b.c.d", 7));
    EXPECT_FALSE(named.has_prefix(OperationID::hash("a.b.c.d.e", 9)));
}

/** @brief Tests that has_prefix matches strict prefixes and never the whole name. */
TEST(Small_PlacementClass, HasPrefixMatchesStrictPrefixesOnly)
{
    constexpr PlacementClass named = "sushiengine.matter.water"_class;
    EXPECT_TRUE(named.has_prefix(OperationID::hash("sushiengine", 11)));
    EXPECT_TRUE(named.has_prefix(OperationID::hash("sushiengine.matter", 18)));
    EXPECT_FALSE(named.has_prefix(named.value));
    EXPECT_FALSE(named.has_prefix(OperationID::hash("sushiai", 7)));
}

/** @brief Tests that the default names nothing and equality compares the value only. */
TEST(Small_PlacementClass, DefaultIsUnnamedAndEqualityComparesTheValue)
{
    constexpr PlacementClass none;
    EXPECT_FALSE(none.is_named());
    EXPECT_EQ(none.value, PlacementClass::K_NONE);
    EXPECT_EQ(none.prefix_count, 0u);
    EXPECT_EQ("sushiai.replica"_class, "sushiai.replica"_class);
    EXPECT_NE("sushiai.replica"_class, "sushiai.trainer"_class);
    EXPECT_NE("sushiai.replica"_class, none);
}
