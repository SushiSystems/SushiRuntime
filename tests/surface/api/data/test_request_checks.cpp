/**************************************************************************/
/* test_request_checks.cpp                                               */
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
 * @file test_request_checks.cpp
 * @brief Tests the predicates a buffer or State request is checked with.
 * @see tests/surface/api/data/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

#include <SushiRuntime/api/data/request_checks.hpp>

using SushiRuntime::fits_byte_count;
using SushiRuntime::is_valid_alignment;
using SushiRuntime::names_a_device;

namespace
{
    /** @brief Holds the largest count of 8-byte elements whose byte size fits. */
    constexpr std::size_t K_MOST_DOUBLES = std::numeric_limits<std::size_t>::max() / sizeof(double);

    TEST(Small_RequestChecks, AnAlignmentOfZeroOrAPowerOfTwoIsValid)
    {
        EXPECT_TRUE(is_valid_alignment(0));
        EXPECT_TRUE(is_valid_alignment(1));
        EXPECT_TRUE(is_valid_alignment(64));
    }

    TEST(Small_RequestChecks, AnAlignmentThatIsNotAPowerOfTwoIsInvalid)
    {
        EXPECT_FALSE(is_valid_alignment(3));
        EXPECT_FALSE(is_valid_alignment(48));
    }

    TEST(Small_RequestChecks, TheLargestCountWhoseByteSizeFitsFits)
    {
        EXPECT_TRUE(fits_byte_count(K_MOST_DOUBLES, sizeof(double)));
        EXPECT_TRUE(fits_byte_count(0, sizeof(double)));
    }

    TEST(Small_RequestChecks, ACountWhoseByteSizeWrapsDoesNotFit)
    {
        EXPECT_FALSE(fits_byte_count(K_MOST_DOUBLES + 1, sizeof(double)));
        EXPECT_FALSE(fits_byte_count(std::numeric_limits<std::size_t>::max(), 2));
    }

    TEST(Small_RequestChecks, AnIndexBelowTheCountNamesADevice)
    {
        EXPECT_TRUE(names_a_device(0, 1));
        EXPECT_TRUE(names_a_device(2, 3));
    }

    TEST(Small_RequestChecks, AnIndexAtOrPastTheCountNamesNoDevice)
    {
        EXPECT_FALSE(names_a_device(1, 1));
        EXPECT_FALSE(names_a_device(0, 0));
        EXPECT_FALSE(names_a_device(5, 3));
    }
} // namespace
