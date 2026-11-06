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
 * @file test_placement_rule.cpp
 * @brief Tests PlacementRule: a rule that states no policy runs its node locally.
 * @author Mustafa Garip
 * @see tests/foundation/placement/README.md
 */

#include <gtest/gtest.h>

#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>

using namespace SushiRuntime;

/** @brief Tests that a rule added with no stated policy runs its node locally. */
TEST(Small_AbsentPolicy, ARuleThatStatesNoPolicyRunsLocally)
{
    PlacementRule rule;
    rule.name = "unstated";
    rule.match.placement_class = "a.b";
    rule.target.machine_name = "worker";
    PlacementRules rules;
    ASSERT_TRUE(rules.add(rule).has_value());

    EXPECT_EQ(rules.rule(0).if_absent.action, AbsentAction::Local);
}
