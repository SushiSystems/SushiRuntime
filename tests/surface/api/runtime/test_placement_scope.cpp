/**************************************************************************/
/* test_placement_scope.cpp                                               */
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

/**
 * @file test_placement_scope.cpp
 * @brief Tests PlacementScope and the two Runtime::placement_scope() overloads.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

static_assert(std::is_nothrow_copy_constructible_v<PlacementScope>,
              "a PlacementScope is a value that copies without throwing");

/** @brief Tests that the accessors return the class and the key the scope was built with. */
TEST(Small_PlacementScope, AccessorsReturnTheClassAndTheKeyItWasBuiltWith)
{
    const PlacementScope scope("sushiai.replica"_class, 5u);

    EXPECT_EQ(scope.placement_class(), "sushiai.replica"_class);
    ASSERT_TRUE(scope.placement_key().has_value());
    EXPECT_EQ(*scope.placement_key(), 5u);
}

/** @brief Tests that a scope built without a key reports none. */
TEST(Small_PlacementScope, ANoneKeyStaysNone)
{
    const PlacementScope scope("sushiai.replica"_class, std::nullopt);

    EXPECT_FALSE(scope.placement_key().has_value());
}

/** @brief Tests that a key of 0 is kept as 0 and is not read as none. */
TEST(Small_PlacementScope, AKeyOfZeroIsKeptAsZero)
{
    const PlacementScope scope("sushiengine.physics"_class, 0u);

    ASSERT_TRUE(scope.placement_key().has_value());
    EXPECT_EQ(*scope.placement_key(), 0u);
}

/** @brief Tests that a copy reports what the original reports. */
TEST(Small_PlacementScope, ACopyReportsTheSameClassAndKey)
{
    const PlacementScope original("sushiengine.physics"_class, 9u);
    const PlacementScope copy = original;

    EXPECT_EQ(copy.placement_class(), original.placement_class());
    EXPECT_EQ(copy.placement_key(), original.placement_key());
}

/** @brief Tests that the string and the class overloads build the same class for one name. */
TEST(Medium_RuntimePlacementScope, TheStringAndClassOverloadsGiveEqualClasses)
{
    auto rt = Runtime::create().value();

    const PlacementScope from_string = rt.placement_scope("sushiai.replica", 2u);
    const PlacementScope from_class = rt.placement_scope("sushiai.replica"_class, 2u);

    EXPECT_EQ(from_string.placement_class(), from_class.placement_class());
    EXPECT_TRUE(from_string.placement_class().is_named());
    EXPECT_EQ(from_string.placement_key(), from_class.placement_key());
}

/** @brief Tests that a key of 0 survives both overloads and a missing key stays none. */
TEST(Medium_RuntimePlacementScope, BothOverloadsKeepAKeyOfZeroAndANoneKey)
{
    auto rt = Runtime::create().value();

    const PlacementScope zero_string = rt.placement_scope("a.b", 0u);
    const PlacementScope zero_class = rt.placement_scope("a.b"_class, 0u);
    const PlacementScope none_string = rt.placement_scope("a.b");
    const PlacementScope none_class = rt.placement_scope("a.b"_class);

    ASSERT_TRUE(zero_string.placement_key().has_value());
    EXPECT_EQ(*zero_string.placement_key(), 0u);
    ASSERT_TRUE(zero_class.placement_key().has_value());
    EXPECT_EQ(*zero_class.placement_key(), 0u);
    EXPECT_FALSE(none_string.placement_key().has_value());
    EXPECT_FALSE(none_class.placement_key().has_value());
}

/** @brief Tests that a name of a single segment builds a class too. */
TEST(Medium_RuntimePlacementScope, ANameWithoutADotIsAClass)
{
    auto rt = Runtime::create().value();

    EXPECT_EQ(rt.placement_scope("physics").placement_class(), "physics"_class);
}

/** @brief Tests that each malformed class name stops the process. */
TEST(Medium_RuntimePlacementScope, AMalformedNameStopsTheProcess)
{
    auto rt = Runtime::create().value();

    EXPECT_DEATH(static_cast<void>(rt.placement_scope("")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope(".a")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope("a.")), "placement_scope");
    EXPECT_DEATH(static_cast<void>(rt.placement_scope("a..b")), "placement_scope");
}

namespace PlacementScopeDeviceTests
{
    namespace
    {
        /** @brief Holds the class every rule of this file matches. */
        constexpr const char* K_CLASS = "device.test";

        /** @brief Returns a rule set of one rule that sends K_CLASS to a target. */
        PlacementRules rules_of(const PlacementTarget& target, AbsentPolicy if_absent,
                                const char* name = "send")
        {
            PlacementRule rule;
            rule.name = name;
            rule.match.placement_class = K_CLASS;
            rule.target = target;
            rule.if_absent = if_absent;
            PlacementRules rules;
            EXPECT_TRUE(rules.add(std::move(rule)));
            return rules;
        }

        /** @brief Returns a target that names the device at an index and no machine. */
        PlacementTarget device_at(std::size_t index)
        {
            PlacementTarget target;
            target.device_index = index;
            return target;
        }

        /** @brief Returns the policy that falls back to device 0 at once. */
        AbsentPolicy local()
        {
            return AbsentPolicy{AbsentAction::Local, std::chrono::milliseconds{0}};
        }

        /** @brief Returns the policy that refuses. */
        AbsentPolicy fail()
        {
            return AbsentPolicy{AbsentAction::Fail, std::chrono::milliseconds{0}};
        }

        /** @brief Returns the policy that waits for a bound, then falls back to device 0. */
        AbsentPolicy wait_for(std::chrono::milliseconds bound)
        {
            return AbsentPolicy{AbsentAction::Wait, bound};
        }

        /** @brief Returns the index one past the last device, which no machine has. */
        std::size_t absent_index(const Runtime& runtime)
        {
            return runtime.advanced().device_count();
        }
    } // namespace

    /** @brief Tests that a scope built without a runtime gives device 0 and refuses a buffer. */
    TEST(Medium_PlacementScopeDevice, AScopeWithoutARuntimeGivesDeviceZeroAndNoBuffer)
    {
        const PlacementScope scope("device.test"_class, std::nullopt);

        const Result<DeviceIndex> device = scope.device();
        ASSERT_TRUE(device);
        EXPECT_EQ(device->value(), 0u);
        const Result<Buffer<int>> buffer = scope.buffer<int>(4);
        ASSERT_FALSE(buffer);
        EXPECT_EQ(buffer.error().code, ErrorCode::InvalidState);
    }

    /** @brief Tests that a scope no rule matches gives device 0, with no rules and with others. */
    TEST(Medium_PlacementScopeDevice, ANoRuleScopeGivesDeviceZero)
    {
        auto rt = Runtime::create().value();
        const PlacementScope scope = rt.placement_scope("other.class");

        EXPECT_EQ(scope.device()->value(), 0u) << "no rules in force";

        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), fail())));
        EXPECT_EQ(scope.device()->value(), 0u) << "the rule names another class";
    }

    /** @brief Tests that a rule with no device part, or a device this machine has, is followed. */
    TEST(Medium_PlacementScopeDevice, ARuleWithAPresentDeviceGivesThatDevice)
    {
        auto rt = Runtime::create().value();
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        PlacementTarget machine_only;
        machine_only.machine_name = rt.topology().name;
        ASSERT_TRUE(rt.set_placement_rules(rules_of(machine_only, fail())));
        EXPECT_EQ(scope.device()->value(), 0u) << "this machine, no device part";

        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(0), fail())));
        EXPECT_EQ(scope.device()->value(), 0u);

        PlacementTarget by_kind;
        by_kind.device_kind = rt.topology().devices.front().kind;
        ASSERT_TRUE(rt.set_placement_rules(rules_of(by_kind, fail())));
        EXPECT_EQ(scope.device()->value(), 0u) << "the first device of device 0's kind";

        PlacementTarget by_memory;
        by_memory.device_min_memory = 1;
        ASSERT_TRUE(rt.set_placement_rules(rules_of(by_memory, fail())));
        EXPECT_EQ(scope.device()->value(), 0u) << "the first device with at least one byte";
    }

    /** @brief Tests that Local gives device 0 when the rule's device is absent. */
    TEST(Medium_PlacementScopeDevice, LocalGivesDeviceZeroForAnAbsentDevice)
    {
        auto rt = Runtime::create().value();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), local())));

        const Result<DeviceIndex> device = rt.placement_scope(K_CLASS).device();

        ASSERT_TRUE(device);
        EXPECT_EQ(device->value(), 0u);
    }

    /** @brief Tests that Fail names the rule and the device in an InvalidArgument error. */
    TEST(Medium_PlacementScopeDevice, FailNamesTheRuleAndTheDevice)
    {
        auto rt = Runtime::create().value();
        const std::size_t absent = absent_index(rt);
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent), fail(), "pin-physics")));

        const Result<DeviceIndex> device = rt.placement_scope(K_CLASS).device();

        ASSERT_FALSE(device);
        EXPECT_EQ(device.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(device.error().message.find("\"pin-physics\""), std::string::npos)
            << device.error().message;
        EXPECT_NE(device.error().message.find("device index " + std::to_string(absent)),
                  std::string::npos)
            << device.error().message;
    }

    /** @brief Tests that an unnamed rule is named by its position in the Fail error. */
    TEST(Medium_PlacementScopeDevice, FailNamesAnUnnamedRuleByItsPosition)
    {
        auto rt = Runtime::create().value();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), fail(), "")));

        const Result<DeviceIndex> device = rt.placement_scope(K_CLASS).device();

        ASSERT_FALSE(device);
        EXPECT_NE(device.error().message.find("at position 0"), std::string::npos)
            << device.error().message;
    }

    /** @brief Tests that Wait gives device 0 only after its bound has passed. */
    TEST(Medium_PlacementScopeDevice, WaitGivesDeviceZeroAfterItsBound)
    {
        constexpr std::chrono::milliseconds K_BOUND{40};
        auto rt = Runtime::create().value();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)),
                                                    wait_for(K_BOUND))));
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        const auto start = std::chrono::steady_clock::now();
        const Result<DeviceIndex> device = scope.device();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        ASSERT_TRUE(device);
        EXPECT_EQ(device->value(), 0u);
        EXPECT_GE(elapsed, K_BOUND);
    }

    /** @brief Tests that a machine another name or label names makes the target absent here. */
    TEST(Medium_PlacementScopeDevice, AnotherMachineMakesTheTargetAbsent)
    {
        auto rt = Runtime::create().value();
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        PlacementTarget other_name = device_at(0);
        other_name.machine_name = rt.topology().name + "-elsewhere";
        ASSERT_TRUE(rt.set_placement_rules(rules_of(other_name, fail())));
        const Result<DeviceIndex> by_name = scope.device();
        ASSERT_FALSE(by_name);
        EXPECT_EQ(by_name.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(by_name.error().message.find(other_name.machine_name), std::string::npos);

        PlacementTarget other_label = device_at(0);
        other_label.machine_label = NodeLabel{"zone", "nowhere"};
        ASSERT_TRUE(rt.set_placement_rules(rules_of(other_label, fail())));
        EXPECT_FALSE(scope.device());

        ASSERT_TRUE(rt.set_placement_rules(rules_of(other_name, local())));
        EXPECT_EQ(scope.device()->value(), 0u);
    }

    /** @brief Tests that a label this machine carries makes the target present. */
    TEST(Medium_PlacementScopeDevice, ALabelTheMachineCarriesMakesTheTargetPresent)
    {
        RuntimeConfig config;
        config.labels = {NodeLabel{"zone", "lab"}};
        auto rt = Runtime::create(config).value();
        PlacementTarget target = device_at(0);
        target.machine_label = NodeLabel{"zone", "lab"};
        ASSERT_TRUE(rt.set_placement_rules(rules_of(target, fail())));

        EXPECT_EQ(rt.placement_scope(K_CLASS).device()->value(), 0u);
    }

    /** @brief Tests that a device of a kind this machine lacks, or too much memory, is absent. */
    TEST(Medium_PlacementScopeDevice, AKindOrMemoryNoDeviceHasIsAbsent)
    {
        auto rt = Runtime::create().value();
        const PlacementScope scope = rt.placement_scope(K_CLASS);

        PlacementTarget huge;
        huge.device_min_memory = std::numeric_limits<std::uint64_t>::max();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(huge, fail())));
        EXPECT_FALSE(scope.device());

        bool has_accelerator = false;
        for (const DeviceFacts& facts : rt.topology().devices)
            has_accelerator = has_accelerator || facts.kind == DeviceKind::Accelerator;
        if (has_accelerator)
            GTEST_SKIP() << "this machine has an accelerator, so the kind is not absent";
        PlacementTarget accelerator;
        accelerator.device_kind = DeviceKind::Accelerator;
        ASSERT_TRUE(rt.set_placement_rules(rules_of(accelerator, fail())));
        EXPECT_FALSE(scope.device());
    }

    /** @brief Tests that a scope opened before set_placement_rules() follows the new set. */
    TEST(Medium_PlacementScopeDevice, AScopeOpenedBeforeTheRulesFollowsTheNewSet)
    {
        auto rt = Runtime::create().value();
        const PlacementScope scope = rt.placement_scope(K_CLASS);
        ASSERT_TRUE(scope.device());

        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), fail())));
        EXPECT_FALSE(scope.device()) << "the new set refuses";

        ASSERT_TRUE(rt.clear_placement_rules());
        EXPECT_TRUE(scope.device()) << "the cleared set leaves nothing to refuse";
    }

    /** @brief Tests that a scope's buffer lands on device 0 under Local. */
    TEST(Medium_PlacementScopeDevice, LocalPlacesTheBufferOnDeviceZero)
    {
        auto rt = Runtime::create().value();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), local())));

        const Result<Buffer<int>> buffer = rt.placement_scope(K_CLASS).buffer<int>(16);

        ASSERT_TRUE(buffer);
        EXPECT_EQ(buffer->device_index().value(), 0u);
        EXPECT_EQ(buffer->size(), 16u);
    }

    /** @brief Tests that Fail returns the error of device() and allocates nothing. */
    TEST(Medium_PlacementScopeDevice, FailRefusesTheBuffer)
    {
        auto rt = Runtime::create().value();
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(absent_index(rt)), fail())));

        const Result<Buffer<int>> buffer = rt.placement_scope(K_CLASS).buffer<int>(16);

        ASSERT_FALSE(buffer);
        EXPECT_EQ(buffer.error().code, ErrorCode::InvalidArgument);
    }

    /** @brief Tests that a scope's buffer lands on device 1 on a machine that has one. */
    TEST(Medium_PlacementScopeDevice, ABufferLandsOnTheDeviceTheRuleNames)
    {
        auto rt = Runtime::create().value();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs a second device for the rule's target";
        ASSERT_TRUE(rt.set_placement_rules(rules_of(device_at(1), fail())));

        const Result<Buffer<int>> buffer = rt.placement_scope(K_CLASS).buffer<int>(16);

        ASSERT_TRUE(buffer);
        EXPECT_EQ(buffer->device_index().value(), 1u);
    }
} // namespace PlacementScopeDeviceTests
