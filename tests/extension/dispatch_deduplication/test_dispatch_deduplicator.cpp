/**************************************************************************/
/* test_dispatch_deduplicator.cpp                                         */
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
 * @file test_dispatch_deduplicator.cpp
 * @brief Tests DispatchDeduplicator: repeats refused, the capacity bound and clear().
 * @see tests/extension/dispatch_deduplication/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/dispatch_deduplication/dispatch_deduplicator.hpp>

using namespace SushiRuntime::Distributed;

/** @brief Tests that a repeated id is refused while it is remembered. */
TEST(Small_DispatchDeduplicatorTest, ARepeatedIDIsRefusedWhileRemembered)
{
    DispatchDeduplicator seen;
    ASSERT_TRUE(seen.mark_seen(1));

    EXPECT_FALSE(seen.mark_seen(1)) << "a repeated dispatch would run twice";

    EXPECT_TRUE(seen.mark_seen(2));
    EXPECT_EQ(seen.size(), 2u);
}

/** @brief Tests that the oldest id is forgotten once the capacity is reached. */
TEST(Small_DispatchDeduplicatorTest, TheOldestIDIsForgottenAtTheCapacity)
{
    DispatchDeduplicator bounded(2);
    ASSERT_TRUE(bounded.mark_seen(1));
    ASSERT_TRUE(bounded.mark_seen(2));

    EXPECT_TRUE(bounded.mark_seen(3));

    EXPECT_EQ(bounded.size(), 2u);
    EXPECT_TRUE(bounded.mark_seen(1)) << "the oldest id was not forgotten at the capacity";
    EXPECT_FALSE(bounded.mark_seen(3));
}

/** @brief Tests that clear() forgets every id, as a new master session needs. */
TEST(Small_DispatchDeduplicatorTest, ClearForgetsEveryId)
{
    DispatchDeduplicator seen;
    ASSERT_TRUE(seen.mark_seen(7));

    seen.clear();

    EXPECT_EQ(seen.size(), 0u);
    EXPECT_TRUE(seen.mark_seen(7));
}
