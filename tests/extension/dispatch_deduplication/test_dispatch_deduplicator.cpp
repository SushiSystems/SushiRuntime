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
 * @file test_dispatch_deduplicator.cpp
 * @brief Tests DispatchDeduplicator: repeats refused, the capacity bound and clear().
 * @see tests/extension/dispatch_deduplication/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/dispatch_deduplication/dispatch_deduplicator.hpp>

using namespace SushiRuntime::Distributed;

/** @brief Tests that a repeated id is refused while remembered, and the oldest goes first. */
TEST(Unit_DispatchDeduplicatorTest, DuplicateDispatchRunsOnce)
{
    DispatchDeduplicator seen;
    EXPECT_TRUE(seen.mark_seen(1));
    EXPECT_FALSE(seen.mark_seen(1)) << "a repeated dispatch would run twice";
    EXPECT_TRUE(seen.mark_seen(2));
    EXPECT_EQ(seen.size(), 2u);

    DispatchDeduplicator bounded(2);
    EXPECT_TRUE(bounded.mark_seen(1));
    EXPECT_TRUE(bounded.mark_seen(2));
    EXPECT_TRUE(bounded.mark_seen(3));
    EXPECT_EQ(bounded.size(), 2u);
    EXPECT_TRUE(bounded.mark_seen(1)) << "the oldest id was not forgotten at the capacity";
    EXPECT_FALSE(bounded.mark_seen(3));
}

/** @brief Tests that clear() forgets every id, as a new master session needs. */
TEST(Unit_DispatchDeduplicatorTest, ClearForgetsEveryId)
{
    DispatchDeduplicator seen;
    EXPECT_TRUE(seen.mark_seen(7));
    seen.clear();
    EXPECT_EQ(seen.size(), 0u);
    EXPECT_TRUE(seen.mark_seen(7));
}
