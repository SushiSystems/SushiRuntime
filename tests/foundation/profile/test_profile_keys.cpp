/**************************************************************************/
/* test_profile_keys.cpp                                                  */
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
 * @file test_profile_keys.cpp
 * @brief Tests that the names the runtime writes into a record are listed once each.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include <SushiRuntime/profile/profile_keys.hpp>

using namespace SushiRuntime::Internal::Profile;

/** @brief Tests that no name is listed twice and none is empty. */
TEST(Small_ProfileKeys, EveryKeyIsListedOnce)
{
    std::set<std::string_view> seen;
    for (const std::string_view key : K_ALL_KEYS)
    {
        EXPECT_FALSE(key.empty());
        EXPECT_TRUE(seen.insert(key).second) << "duplicate key " << key;
    }
    EXPECT_EQ(seen.size(), K_ALL_KEYS.size());
}

/** @brief Tests the spelling of the names a profile reader looks for. */
TEST(Small_ProfileKeys, TheNamesAreSpelledAsTheProfileReaderExpects)
{
    EXPECT_EQ(K_HOST_TRACK_NAME, "host");
    EXPECT_EQ(K_KEY_LAYER, "layer");
    EXPECT_EQ(K_LAYER_SCHEDULER, "scheduler");
    EXPECT_EQ(K_LAYER_CHAIN, "chain");
}
