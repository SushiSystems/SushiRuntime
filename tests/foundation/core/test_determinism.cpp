/**************************************************************************/
/* test_determinism.cpp                                                   */
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
 * @file test_determinism.cpp
 * @brief Tests Determinism: distinct enumerators, the zero default and one byte of storage.
 * @author Mustafa Garip
 * @see tests/foundation/core/README.md
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/determinism.hpp>

using namespace SushiRuntime;

/** @brief Tests that the four enumerators hold four distinct values. */
TEST(Small_Determinism, EnumeratorsHoldDistinctValues)
{
    const std::uint8_t unstated = static_cast<std::uint8_t>(Determinism::Unstated);
    const std::uint8_t bitwise = static_cast<std::uint8_t>(Determinism::Bitwise);
    const std::uint8_t tolerant = static_cast<std::uint8_t>(Determinism::Tolerant);
    const std::uint8_t cosmetic = static_cast<std::uint8_t>(Determinism::Cosmetic);
    EXPECT_NE(unstated, bitwise);
    EXPECT_NE(unstated, tolerant);
    EXPECT_NE(unstated, cosmetic);
    EXPECT_NE(bitwise, tolerant);
    EXPECT_NE(bitwise, cosmetic);
    EXPECT_NE(tolerant, cosmetic);
}

/** @brief Tests that Unstated is the value 0, so a value-initialised field states nothing. */
TEST(Small_Determinism, UnstatedIsTheZeroValue)
{
    EXPECT_EQ(static_cast<std::uint8_t>(Determinism::Unstated), 0u);
    EXPECT_EQ(Determinism{}, Determinism::Unstated);
}

/** @brief Tests that the type is one byte and an unsigned 8-bit integer underneath. */
TEST(Small_Determinism, IsOneUnsignedByte)
{
    static_assert(sizeof(Determinism) == 1, "Determinism is one byte");
    static_assert(std::is_same<std::underlying_type_t<Determinism>, std::uint8_t>::value,
                  "Determinism is backed by std::uint8_t");
    EXPECT_EQ(sizeof(Determinism), 1u);
}
