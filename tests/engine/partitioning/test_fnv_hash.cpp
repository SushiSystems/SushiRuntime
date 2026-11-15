/**************************************************************************/
/* test_fnv_hash.cpp                                                      */
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

/**
 * @file test_fnv_hash.cpp
 * @brief Tests that FNVHash folds a value as its eight little-endian bytes under FNV-1a.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <SushiRuntime/partitioning/fnv_hash.hpp>

using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the FNV-1a 64-bit hash of bytes, byte by byte, with the published constants. */
    std::uint64_t reference_fnv1a(const std::vector<std::uint8_t>& bytes)
    {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (const std::uint8_t byte : bytes)
        {
            hash ^= byte;
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    TEST(Small_FNVHash, FoldingTwoValuesHashesTheirSixteenLittleEndianBytes)
    {
        FNVHash hash;
        hash.fold(0x0807060504030201ULL);
        hash.fold(0x10U);

        const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5, 6, 7, 8, 0x10, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_EQ(hash.value(), reference_fnv1a(bytes));
    }
} // namespace
