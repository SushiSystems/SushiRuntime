/**************************************************************************/
/* test_dispatch_width.cpp                                                */
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
 * @file test_dispatch_width.cpp
 * @brief Tests K_WIDTH_UNKNOWN: the sentinel no real launch width reaches.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <SushiRuntime/report/dispatch_width.hpp>

using namespace SushiRuntime;

/** @brief Tests that the unknown width is the largest 32-bit value. */
TEST(Small_DispatchWidth, UnknownIsTheLargestWidth)
{
    static_assert(K_WIDTH_UNKNOWN == std::numeric_limits<std::uint32_t>::max(),
                  "the unknown width is the one value no launch counts to");
    SUCCEED();
}
