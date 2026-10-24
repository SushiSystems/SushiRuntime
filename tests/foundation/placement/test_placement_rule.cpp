/**************************************************************************/
/* test_placement_rule.cpp                                                */
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
 * @file test_placement_rule.cpp
 * @brief Tests PlacementRule and its records: the values they hold before a caller fills them.
 * @author Mustafa Garip
 * @see tests/foundation/placement/README.md
 */

#include <chrono>

#include <gtest/gtest.h>

#include <SushiRuntime/placement/placement_rule.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default policy runs the node locally with no wait bound. */
TEST(Small_AbsentPolicy, DefaultsRunLocallyWithNoBound)
{
    const AbsentPolicy policy;
    EXPECT_EQ(policy.action, AbsentAction::Local);
    EXPECT_EQ(policy.bound, std::chrono::milliseconds(0));
}

/** @brief Tests that a default match states no class, key or operation. */
TEST(Small_PlacementMatch, DefaultsStateNothing)
{
    const PlacementMatch match;
    EXPECT_TRUE(match.placement_class.empty());
    EXPECT_FALSE(match.key.has_value());
    EXPECT_TRUE(match.operation.empty());
}

/** @brief Tests that a default target names no machine and no device. */
TEST(Small_PlacementTarget, DefaultsNameNothing)
{
    const PlacementTarget target;
    EXPECT_TRUE(target.machine_name.empty());
    EXPECT_FALSE(target.machine_label.has_value());
    EXPECT_FALSE(target.device_index.has_value());
    EXPECT_FALSE(target.device_kind.has_value());
    EXPECT_FALSE(target.device_min_memory.has_value());
}

/** @brief Tests that a default rule has no name, priority 0, a local policy and no deadline. */
TEST(Small_PlacementRule, DefaultsHoldNoNamePriorityZeroAndALocalPolicy)
{
    const PlacementRule rule;
    EXPECT_TRUE(rule.name.empty());
    EXPECT_EQ(rule.priority, 0);
    EXPECT_EQ(rule.if_absent.action, AbsentAction::Local);
    EXPECT_FALSE(rule.deadline.has_value());
    EXPECT_TRUE(rule.match.placement_class.empty());
    EXPECT_TRUE(rule.target.machine_name.empty());
}
