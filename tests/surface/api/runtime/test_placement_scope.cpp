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

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

static_assert(std::is_nothrow_copy_constructible_v<PlacementScope>,
              "a PlacementScope is a value that copies without throwing");

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

    /** @brief Returns the policy that waits for the target for at most a bound. */
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

/** @brief Tests that a scope built without a runtime gives device 0. */
TEST(Small_PlacementScope, AScopeWithoutARuntimeGivesDeviceZero)
{
    const PlacementScope scope("device.test"_class, std::nullopt);

    const Result<DeviceIndex> device = scope.device();

    ASSERT_TRUE(device);
    EXPECT_EQ(device->value(), 0u);
}

/** @brief Tests that a scope built without a runtime refuses a buffer with InvalidState. */
TEST(Small_PlacementScope, AScopeWithoutARuntimeRefusesABuffer)
{
    const PlacementScope scope("device.test"_class, std::nullopt);

    const Result<Buffer<int>> buffer = scope.buffer<int>(4);

    ASSERT_FALSE(buffer);
    EXPECT_EQ(buffer.error().code, ErrorCode::InvalidState);
}

/** @brief Fixture of the scope tests that build a scope from the program's runtime. */
class Medium_RuntimePlacementScope : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the string and the class overloads build the same class for one name. */
TEST_F(Medium_RuntimePlacementScope, TheStringAndClassOverloadsGiveEqualClasses)
{
    const PlacementScope from_string = runtime().placement_scope("sushiai.replica", 2u);
    const PlacementScope from_class = runtime().placement_scope("sushiai.replica"_class, 2u);

    EXPECT_EQ(from_string.placement_class(), from_class.placement_class());
    EXPECT_TRUE(from_string.placement_class().is_named());
    EXPECT_EQ(from_string.placement_key(), from_class.placement_key());
}

/** @brief Tests that a key of 0 survives both overloads and a missing key stays none. */
TEST_F(Medium_RuntimePlacementScope, BothOverloadsKeepAKeyOfZeroAndANoneKey)
{
    const PlacementScope zero_string = runtime().placement_scope("a.b", 0u);
    const PlacementScope zero_class = runtime().placement_scope("a.b"_class, 0u);
    const PlacementScope none_string = runtime().placement_scope("a.b");
    const PlacementScope none_class = runtime().placement_scope("a.b"_class);

    ASSERT_TRUE(zero_string.placement_key().has_value());
    EXPECT_EQ(*zero_string.placement_key(), 0u);
    ASSERT_TRUE(zero_class.placement_key().has_value());
    EXPECT_EQ(*zero_class.placement_key(), 0u);
    EXPECT_FALSE(none_string.placement_key().has_value());
    EXPECT_FALSE(none_class.placement_key().has_value());
}

/** @brief Tests that a name of a single segment builds a class too. */
TEST_F(Medium_RuntimePlacementScope, ANameWithoutADotIsAClass)
{
    EXPECT_EQ(runtime().placement_scope("physics").placement_class(), "physics"_class);
}

/** @brief Fixture of the death test of a malformed class name; the parameter is the name. */
class Medium_RuntimePlacementScopeDeathTest : public TestSupport::RuntimeTest,
                                              public ::testing::WithParamInterface<const char*>
{
};

/** @brief Tests that a malformed class name stops the process. */
TEST_P(Medium_RuntimePlacementScopeDeathTest, AMalformedNameStopsTheProcess)
{
    EXPECT_DEATH(static_cast<void>(runtime().placement_scope(GetParam())), "placement_scope");
}

INSTANTIATE_TEST_SUITE_P(Names, Medium_RuntimePlacementScopeDeathTest,
                         ::testing::Values("", ".a", "a.", "a..b"));

/** @brief Fixture of the tests of the device a scope resolves to; each runs with no rules in force. */
class Medium_PlacementScopeDevice : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Clears the rules the program's runtime may hold from an earlier test. */
        void SetUp() override { static_cast<void>(runtime().clear_placement_rules()); }

        /** @brief Clears the rules this test set, so the next test starts without them. */
        void TearDown() override { static_cast<void>(runtime().clear_placement_rules()); }
};

/** @brief Tests that a scope no rule exists for gives device 0. */
TEST_F(Medium_PlacementScopeDevice, ANoRuleScopeGivesDeviceZero)
{
    const PlacementScope scope = runtime().placement_scope("other.class");

    EXPECT_EQ(scope.device()->value(), 0u);
}

/** @brief Tests that a scope whose class no rule names gives device 0 whatever the rules say. */
TEST_F(Medium_PlacementScopeDevice, ARuleForAnotherClassLeavesDeviceZero)
{
    const PlacementScope scope = runtime().placement_scope("other.class");
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), fail())));

    EXPECT_EQ(scope.device()->value(), 0u);
}

/** @brief Holds one target a machine can always satisfy, as a function of the runtime it is built for. */
struct PresentTarget
{
    const char* name;                              /**< Names the row. */
    PlacementTarget (*build)(const Runtime&);      /**< Builds the target. */
};

/** @brief Fixture of the tests of a rule whose target this machine has; the parameter is the row. */
class Medium_PlacementScopePresentTarget : public Medium_PlacementScopeDevice,
                                           public ::testing::WithParamInterface<PresentTarget>
{
};

/** @brief Tests that a rule with no device part, or a device this machine has, gives device 0. */
TEST_P(Medium_PlacementScopePresentTarget, ARuleWithAPresentTargetGivesDeviceZero)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(GetParam().build(runtime()), fail())));

    EXPECT_EQ(runtime().placement_scope(K_CLASS).device()->value(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    Targets, Medium_PlacementScopePresentTarget,
    ::testing::Values(
        PresentTarget{"ThisMachineWithNoDevicePart",
                      [](const Runtime& r)
                      {
                          PlacementTarget target;
                          target.machine_name = r.topology().name;
                          return target;
                      }},
        PresentTarget{"DeviceZero", [](const Runtime&) { return device_at(0); }},
        PresentTarget{"TheKindOfDeviceZero",
                      [](const Runtime& r)
                      {
                          PlacementTarget target;
                          target.device_kind = r.topology().devices.front().kind;
                          return target;
                      }},
        PresentTarget{"AtLeastOneByteOfMemory",
                      [](const Runtime&)
                      {
                          PlacementTarget target;
                          target.device_min_memory = 1;
                          return target;
                      }}),
    [](const ::testing::TestParamInfo<PresentTarget>& info) { return std::string(info.param.name); });

/** @brief Tests that Local gives device 0 when the rule's device is absent. */
TEST_F(Medium_PlacementScopeDevice, LocalGivesDeviceZeroForAnAbsentDevice)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), local())));

    const Result<DeviceIndex> device = runtime().placement_scope(K_CLASS).device();

    ASSERT_TRUE(device);
    EXPECT_EQ(device->value(), 0u);
}

/** @brief Tests that Fail names the rule and the device in an InvalidArgument error. */
TEST_F(Medium_PlacementScopeDevice, FailNamesTheRuleAndTheDevice)
{
    const std::size_t absent = absent_index(runtime());
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent), fail(), "pin-physics")));

    const Result<DeviceIndex> device = runtime().placement_scope(K_CLASS).device();

    ASSERT_FALSE(device);
    EXPECT_EQ(device.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(device.error().message.find("\"pin-physics\""), std::string::npos)
        << device.error().message;
    EXPECT_NE(device.error().message.find("device index " + std::to_string(absent)),
              std::string::npos)
        << device.error().message;
}

/** @brief Tests that Wait gives device 0, since a local device never appears. */
TEST_F(Medium_PlacementScopeDevice, WaitGivesDeviceZeroForAnAbsentDevice)
{
    constexpr std::chrono::milliseconds K_BOUND{std::chrono::minutes(10)};
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())),
                                                       wait_for(K_BOUND))));

    const Result<DeviceIndex> device = runtime().placement_scope(K_CLASS).device();

    ASSERT_TRUE(device);
    EXPECT_EQ(device->value(), 0u);
}

/** @brief Tests that a machine name another machine carries makes the target absent and Fail names it. */
TEST_F(Medium_PlacementScopeDevice, AnotherMachinesNameMakesTheTargetAbsent)
{
    PlacementTarget other_name = device_at(0);
    other_name.machine_name = runtime().topology().name + "-elsewhere";
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(other_name, fail())));

    const Result<DeviceIndex> device = runtime().placement_scope(K_CLASS).device();

    ASSERT_FALSE(device);
    EXPECT_EQ(device.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(device.error().message.find(other_name.machine_name), std::string::npos);
}

/** @brief Tests that a label this machine lacks makes the target absent. */
TEST_F(Medium_PlacementScopeDevice, ALabelNoMachineCarriesMakesTheTargetAbsent)
{
    PlacementTarget other_label = device_at(0);
    other_label.machine_label = NodeLabel{"zone", "nowhere"};
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(other_label, fail())));

    EXPECT_FALSE(runtime().placement_scope(K_CLASS).device());
}

/** @brief Tests that Local falls back to device 0 when the machine is another one. */
TEST_F(Medium_PlacementScopeDevice, LocalFallsBackWhenTheMachineIsAnotherOne)
{
    PlacementTarget other_name = device_at(0);
    other_name.machine_name = runtime().topology().name + "-elsewhere";
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(other_name, local())));

    EXPECT_EQ(runtime().placement_scope(K_CLASS).device()->value(), 0u);
}

/** @brief Tests that a label the machine carries makes the target present. */
TEST(Medium_PlacementScopeLabel, ALabelTheMachineCarriesMakesTheTargetPresent)
{
    RuntimeConfig config;
    config.labels = {NodeLabel{"zone", "lab"}};
    auto labelled = Runtime::create(config).value();
    PlacementTarget target = device_at(0);
    target.machine_label = NodeLabel{"zone", "lab"};
    ASSERT_TRUE(labelled.set_placement_rules(rules_of(target, fail())));

    EXPECT_EQ(labelled.placement_scope(K_CLASS).device()->value(), 0u);
}

/** @brief Tests that a scope opened before a refusing rule set follows it. */
TEST_F(Medium_PlacementScopeDevice, AScopeOpenedBeforeARefusingSetIsRefusedByIt)
{
    const PlacementScope scope = runtime().placement_scope(K_CLASS);
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), fail())));

    EXPECT_FALSE(scope.device());
}

/** @brief Tests that a scope opened before the rules are cleared follows the cleared set. */
TEST_F(Medium_PlacementScopeDevice, AScopeOpenedBeforeTheRulesAreClearedIsNoLongerRefused)
{
    const PlacementScope scope = runtime().placement_scope(K_CLASS);
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), fail())));
    ASSERT_TRUE(runtime().clear_placement_rules());

    EXPECT_TRUE(scope.device());
}

/** @brief Tests that a scope's buffer lands on device 0 under Local. */
TEST_F(Medium_PlacementScopeDevice, LocalPlacesTheBufferOnDeviceZero)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), local())));

    const Result<Buffer<int>> buffer = runtime().placement_scope(K_CLASS).buffer<int>(16);

    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->device_index().value(), 0u);
    EXPECT_EQ(buffer->size(), 16u);
}

/** @brief Tests that Fail refuses the buffer with the error of device(). */
TEST_F(Medium_PlacementScopeDevice, FailRefusesTheBuffer)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), fail())));

    const Result<Buffer<int>> buffer = runtime().placement_scope(K_CLASS).buffer<int>(16);

    ASSERT_FALSE(buffer);
    EXPECT_EQ(buffer.error().code, ErrorCode::InvalidArgument);
}

/** @brief Tests that a refused buffer allocates nothing. */
TEST_F(Medium_PlacementScopeDevice, FailAllocatesNothing)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(absent_index(runtime())), fail())));
    const std::size_t before = runtime().advanced().live_allocation_count();

    static_cast<void>(runtime().placement_scope(K_CLASS).buffer<int>(16));

    EXPECT_EQ(runtime().advanced().live_allocation_count(), before);
}

/** @brief Fixture of the tests that place a buffer on device 1, which the machine must have. */
class Medium_PlacementScopeSecondDevice : public Medium_PlacementScopeDevice
{
    protected:
        /** @brief Skips the test on a machine with one device. */
        void SetUp() override
        {
            Medium_PlacementScopeDevice::SetUp();
            if (runtime().advanced().device_count() < 2)
                GTEST_SKIP() << "needs a second device for the rule's target";
        }
};

/** @brief Tests that a scope's buffer lands on device 1 when the rule names it. */
TEST_F(Medium_PlacementScopeSecondDevice, ABufferLandsOnTheDeviceTheRuleNames)
{
    ASSERT_TRUE(runtime().set_placement_rules(rules_of(device_at(1), fail())));

    const Result<Buffer<int>> buffer = runtime().placement_scope(K_CLASS).buffer<int>(16);

    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->device_index().value(), 1u);
}
