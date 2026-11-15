/**************************************************************************/
/* test_file_memory_space.cpp                                             */
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
 * @file test_file_memory_space.cpp
 * @brief Tests that FileMemorySpace reports its range as capacity, reaches no space directly and maps instance addresses to file offsets.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/memory/file_memory_space.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    TEST(Small_FileMemorySpace, ReportsTheRangeAsCapacityAndNoReserve)
    {
        const FileMemorySpace space(MemorySpaceID(5), FileRange{"weights.bin", 128, 4096});

        EXPECT_EQ(space.id(), MemorySpaceID(5));
        EXPECT_EQ(space.kind_name(), "file");
        EXPECT_EQ(space.capacity_bytes(), 4096U);
        EXPECT_EQ(space.reserve_bytes(), 0U);
        EXPECT_EQ(space.shared_pool(), 5U);
        EXPECT_TRUE(space.direct_targets().empty());
        EXPECT_EQ(space.range().path, "weights.bin");
    }

    TEST(Small_FileMemorySpace, MapsAnAddressInsideTheInstanceToItsFileOffset)
    {
        const FileMemorySpace space(MemorySpaceID(5), FileRange{"weights.bin", 128, 4096});
        const auto* const base = static_cast<const unsigned char*>(FileMemorySpace::instance_address());

        EXPECT_NE(FileMemorySpace::instance_address(), nullptr);
        EXPECT_EQ(space.file_offset(base), 128U);
        EXPECT_EQ(space.file_offset(base + 1000), 1128U);
    }
} // namespace
