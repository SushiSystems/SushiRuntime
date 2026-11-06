/**************************************************************************/
/* test_common.cpp                                                        */
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
 * @file test_common.cpp
 * @brief Tests the powers of two the alignment constants of common.hpp promise.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/core/common.hpp>

using namespace SushiRuntime;

/** @brief Tests that both alignments are powers of two and the default covers a cache line. */
TEST(Small_Common, AlignmentsArePowersOfTwo)
{
    const auto is_power_of_two = [](std::size_t value) { return value != 0 && (value & (value - 1)) == 0; };
    EXPECT_TRUE(is_power_of_two(K_CACHE_LINE_SIZE));
    EXPECT_TRUE(is_power_of_two(K_DEFAULT_ALIGNMENT));
    EXPECT_EQ(K_DEFAULT_ALIGNMENT % K_CACHE_LINE_SIZE, 0u);
}
