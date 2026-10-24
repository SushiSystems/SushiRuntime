/**************************************************************************/
/* test_placement_scope.cpp                                               */
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

/**
 * @file test_placement_scope.cpp
 * @brief Tests PlacementScope and the two Runtime::placement_scope() overloads.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

static_assert(std::is_trivially_copyable_v<PlacementScope>,
              "a PlacementScope is a value that copies without an owner");

/** @brief Tests that the accessors return the class and the key the scope was built with. */
TEST(Small_PlacementScope, AccessorsReturnTheClassAndTheKeyItWasBuiltWith)
{
    const PlacementScope scope("sushiai.replica"_class, 5u);

    EXPECT_EQ(scope.placement_class(), "sushiai.replica"_class);
    ASSERT_TRUE(scope.placement_key().has_value());
    EXPECT_EQ(*scope.placement_key(), 5u);
}

/** @brief Tests that a scope built without a key reports none. */
TEST(Small_PlacementScope, ANoneKeyStaysNone)
{
    const PlacementScope scope("sushiai.replica"_class, std::nullopt);

    EXPECT_FALSE(scope.placement_key().has_value());
}

/** @brief Tests that a key of 0 is kept as 0 and is not read as none. */
TEST(Small_PlacementScope, AKeyOfZeroIsKeptAsZero)
{
    const PlacementScope scope("sushiengine.physics"_class, 0u);

    ASSERT_TRUE(scope.placement_key().has_value());
    EXPECT_EQ(*scope.placement_key(), 0u);
}

/** @brief Tests that a copy reports what the original reports. */
TEST(Small_PlacementScope, ACopyReportsTheSameClassAndKey)
{
    const PlacementScope original("sushiengine.physics"_class, 9u);
    const PlacementScope copy = original;

    EXPECT_EQ(copy.placement_class(), original.placement_class());
    EXPECT_EQ(copy.placement_key(), original.placement_key());
}

/** @brief Tests that the string and the class overloads build the same class for one name. */
TEST(Medium_RuntimePlacementScope, TheStringAndClassOverloadsGiveEqualClasses)
{
    auto rt = Runtime::create().value();

    const PlacementScope from_string = rt.placement_scope("sushiai.replica", 2u);
    const PlacementScope from_class = rt.placement_scope("sushiai.replica"_class, 2u);

    EXPECT_EQ(from_string.placement_class(), from_class.placement_class());
    EXPECT_TRUE(from_string.placement_class().is_named());
    EXPECT_EQ(from_string.placement_key(), from_class.placement_key());
}

/** @brief Tests that a key of 0 survives both overloads and a missing key stays none. */
TEST(Medium_RuntimePlacementScope, BothOverloadsKeepAKeyOfZeroAndANoneKey)
{
    auto rt = Runtime::create().value();

    const PlacementScope zero_string = rt.placement_scope("a.b", 0u);
    const PlacementScope zero_class = rt.placement_scope("a.b"_class, 0u);
    const PlacementScope none_string = rt.placement_scope("a.b");
    const PlacementScope none_class = rt.placement_scope("a.b"_class);

    ASSERT_TRUE(zero_string.placement_key().has_value());
    EXPECT_EQ(*zero_string.placement_key(), 0u);
    ASSERT_TRUE(zero_class.placement_key().has_value());
    EXPECT_EQ(*zero_class.placement_key(), 0u);
    EXPECT_FALSE(none_string.placement_key().has_value());
    EXPECT_FALSE(none_class.placement_key().has_value());
}

/** @brief Tests that a name of a single segment builds a class too. */
TEST(Medium_RuntimePlacementScope, ANameWithoutADotIsAClass)
{
    auto rt = Runtime::create().value();

    EXPECT_EQ(rt.placement_scope("physics").placement_class(), "physics"_class);
}

/** @brief Tests that each malformed class name stops the process. */
TEST(Medium_RuntimePlacementScope, AMalformedNameStopsTheProcess)
{
    auto rt = Runtime::create().value();

    EXPECT_DEATH(static_cast<void>(rt.placement_scope("")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope(".a")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope("a.")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope("a..b")), "placement_scope");
}
