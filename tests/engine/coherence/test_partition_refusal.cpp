/**************************************************************************/
/* test_partition_refusal.cpp                                             */
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
 * @file test_partition_refusal.cpp
 * @brief Tests that PoolShortfall reports the bytes a pool is short by, and zero when it holds them.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/partition_refusal.hpp>

using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PartitionRefusal, AShortfallIsTheRequiredBytesAboveTheUsableOnes)
    {
        EXPECT_EQ((PoolShortfall{1, 1200, 900}.missing()), 300U);
        EXPECT_EQ((PoolShortfall{1, 900, 900}.missing()), 0U);
        EXPECT_EQ((PoolShortfall{1, 100, 900}.missing()), 0U);
    }
} // namespace
