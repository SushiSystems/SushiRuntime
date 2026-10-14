/**************************************************************************/
/* test_bit.cpp                                                           */
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
 * @file test_bit.cpp
 * @brief Tests popcount_u64() and countr_zero_u64() on edge and mixed words.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/bit.hpp>

using namespace SushiRuntime;

/** @brief Tests the bit count of an empty, a full and a mixed word. */
TEST(Unit_Bit, PopcountCountsEverySetBit)
{
    EXPECT_EQ(popcount_u64(0u), 0);
    EXPECT_EQ(popcount_u64(~std::uint64_t{0}), 64);
    EXPECT_EQ(popcount_u64(0x8000000000000001ull), 2);
    EXPECT_EQ(popcount_u64(0x00F0F0F0F0F0F0F0ull), 28);
}

/** @brief Tests the trailing zero count, including 64 for a word with no bit set. */
TEST(Unit_Bit, CountrZeroFindsTheLowestSetBit)
{
    EXPECT_EQ(countr_zero_u64(0u), 64);
    EXPECT_EQ(countr_zero_u64(1u), 0);
    EXPECT_EQ(countr_zero_u64(0x8000000000000000ull), 63);
    EXPECT_EQ(countr_zero_u64(0x0000000000F00000ull), 20);
}
