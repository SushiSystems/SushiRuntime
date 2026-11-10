/**************************************************************************/
/* test_local_rule_device.cpp                                             */
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
 * @file test_local_rule_device.cpp
 * @brief Tests the device a placement rule gives on a machine and the device a scope resolves to.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/local_rule_device.hpp>
#include <SushiRuntime/core/placement_class.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Holds the class every rule of this file matches. */
    constexpr const char* K_CLASS = "device.test";

    /** @brief Returns a machine of a name and a count of devices. */
    NodeFacts machine_of(const char* name, std::size_t device_count)
    {
        NodeFacts facts;
        facts.name = name;
        facts.devices.resize(device_count);
        return facts;
    }

    /** @brief Returns a rule that sends K_CLASS to the device at an index, with a policy. */
    PlacementRule rule_for(std::size_t device_index, AbsentAction action, const char* name = "send")
    {
        PlacementRule rule;
        rule.name = name;
        rule.match.placement_class = K_CLASS;
        rule.target.device_index = device_index;
        rule.if_absent = AbsentPolicy{action, std::chrono::milliseconds{0}};
        return rule;
    }

    /** @brief Returns a rule set of one rule. */
    PlacementRules rules_of(PlacementRule rule)
    {
        PlacementRules rules;
        EXPECT_TRUE(rules.add(std::move(rule)));
        return rules;
    }

    TEST(Small_LocalRuleDevice, ADevicePastTheMachineGivesNoneUnderLocalAndWait)
    {
        const NodeFacts here = machine_of("m", 2);

        for (const AbsentAction action : {AbsentAction::Local, AbsentAction::Wait})
        {
            const auto device = Detail::local_device_under(rule_for(2, action), 0, here, "caller", "x");

            ASSERT_TRUE(device);
            EXPECT_FALSE(device->has_value());
        }
    }

    TEST(Small_LocalRuleDevice, ADeviceTheMachineHasIsGivenWhateverTheAbsentAction)
    {
        const NodeFacts here = machine_of("m", 2);

        const auto device = Detail::local_device_under(rule_for(1, AbsentAction::Fail), 0, here, "caller", "x");

        ASSERT_TRUE(device);
        EXPECT_EQ(*device, std::optional<std::size_t>{1});
    }

    TEST(Small_LocalRuleDevice, ADevicePastTheMachineIsRefusedUnderFailNamingTheRuleAndTheDevice)
    {
        const NodeFacts here = machine_of("box", 2);

        const auto device =
            Detail::local_device_under(rule_for(5, AbsentAction::Fail, "pin-physics"), 3, here, "Caller::call", "the scope");

        ASSERT_FALSE(device);
        EXPECT_EQ(device.error().code, ErrorCode::InvalidArgument);
        const std::string& message = device.error().message;
        EXPECT_NE(message.find("Caller::call: rule "), std::string::npos) << message;
        EXPECT_NE(message.find("\"pin-physics\""), std::string::npos) << message;
        EXPECT_NE(message.find("places the scope on "), std::string::npos) << message;
        EXPECT_NE(message.find("device index 5"), std::string::npos) << message;
        EXPECT_NE(message.find("machine \"box\" does not have (it has 2 device(s))"), std::string::npos)
            << message;
    }

    TEST(Small_LocalRuleDevice, AScopeOfNoMatchingRuleResolvesToDeviceZero)
    {
        const NodeFacts here = machine_of("m", 1);
        const PlacementRules rules = rules_of(rule_for(0, AbsentAction::Fail));

        const auto device =
            Detail::resolve_scope_device("other.class"_class, std::nullopt, rules, here);

        ASSERT_TRUE(device);
        EXPECT_EQ(*device, 0u);
    }

    TEST(Small_LocalRuleDevice, AScopeResolvesToTheDeviceItsRuleNames)
    {
        const NodeFacts here = machine_of("m", 2);
        const PlacementRules rules = rules_of(rule_for(1, AbsentAction::Fail));

        const auto device =
            Detail::resolve_scope_device("device.test"_class, std::nullopt, rules, here);

        ASSERT_TRUE(device);
        EXPECT_EQ(*device, 1u);
    }

    TEST(Small_LocalRuleDevice, AScopeOfAnAbsentDeviceResolvesToDeviceZeroUnderLocalAndIsRefusedUnderFail)
    {
        const NodeFacts here = machine_of("m", 1);

        const auto local = Detail::resolve_scope_device(
            "device.test"_class, std::nullopt, rules_of(rule_for(4, AbsentAction::Local)), here);
        const auto fail = Detail::resolve_scope_device(
            "device.test"_class, std::nullopt, rules_of(rule_for(4, AbsentAction::Fail)), here);

        ASSERT_TRUE(local);
        EXPECT_EQ(*local, 0u);
        ASSERT_FALSE(fail);
        EXPECT_EQ(fail.error().code, ErrorCode::InvalidArgument);
    }
} // namespace
