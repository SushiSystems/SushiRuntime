/**************************************************************************/
/* test_rule_placement.cpp                                                */
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
 * @file test_rule_placement.cpp
 * @brief Tests RulePlacement: a rule for the master, and each if_absent for an absent worker.
 * @author Mustafa Garip
 * @see tests/extension/offload_coordination/README.md
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>
#include <SushiRuntime/cluster_load/placement_decision.hpp>
#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/node_label.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/rule_placement.hpp>
#include <SushiRuntime/offload_coordination/worker_directory.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Returns a fixed point of the steady clock plus @p offset. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }

    /** @brief Places nodes on a master named "master" with a directory that has no worker. */
    class Small_RulePlacement : public ::testing::Test
    {
        protected:
            /** @brief Returns a rule named "pin" that targets the machine "rack-7". */
            static PlacementRule remote_rule(AbsentAction action)
            {
                PlacementRule rule;
                rule.name = "pin";
                rule.match.placement_class = "test.pinned";
                rule.target.machine_name = "rack-7";
                rule.if_absent.action = action;
                rule.if_absent.bound = milliseconds(40);
                return rule;
            }

            /** @brief Holds the directory the placement asks; no worker registers. */
            WorkerDirectory directory_{span<const std::uint8_t>(),
                                       std::make_shared<LeastLoadedOffloadPolicy>(0)};

            /** @brief Holds the first offers of waiting nodes. */
            HeldNodes held_;

            /** @brief Holds the placement under test. */
            RulePlacement placement_{directory_, held_,
                                     NodeFacts{"master", OperatingSystem::Unknown,
                                               {NodeLabel{"zone", "a"}}, {}}};

            /** @brief Holds the node the placement decides. */
            sushi_ptr<Internal::Graph::Node> node_ = make_sushi<Internal::Graph::Node>();
    };
} // namespace

/** @brief Tests that a rule whose machine part names the master keeps the node local. */
TEST_F(Small_RulePlacement, RuleForTheMasterRunsLocally)
{
    PlacementRule rule = remote_rule(AbsentAction::Fail);
    rule.target.machine_name.clear();
    rule.target.machine_label = NodeLabel{"zone", "a"};
    EXPECT_TRUE(placement_.names_master(rule));
    EXPECT_TRUE(std::holds_alternative<RunLocally>(
        placement_.decide(rule, 0, *node_, at(milliseconds(0)))));
    EXPECT_FALSE(placement_.names_master(remote_rule(AbsentAction::Fail)));
}

/** @brief Tests that Local runs a node whose worker is absent on the master. */
TEST_F(Small_RulePlacement, LocalRunsAnAbsentTargetOnTheMaster)
{
    EXPECT_TRUE(std::holds_alternative<RunLocally>(placement_.decide(
        remote_rule(AbsentAction::Local), 0, *node_, at(milliseconds(0)))));
}

/** @brief Tests that Wait waits until the bound from the first offer, then runs locally. */
TEST_F(Small_RulePlacement, WaitEndsAtTheBoundFromTheFirstOffer)
{
    const PlacementRule rule = remote_rule(AbsentAction::Wait);
    const PlacementDecision first = placement_.decide(rule, 0, *node_, at(milliseconds(0)));
    ASSERT_TRUE(std::holds_alternative<WaitForTarget>(first));
    EXPECT_EQ(std::get<WaitForTarget>(first).until, at(milliseconds(40)));

    const PlacementDecision again = placement_.decide(rule, 0, *node_, at(milliseconds(20)));
    ASSERT_TRUE(std::holds_alternative<WaitForTarget>(again));
    EXPECT_EQ(std::get<WaitForTarget>(again).until, at(milliseconds(40)));

    EXPECT_TRUE(std::holds_alternative<RunLocally>(
        placement_.decide(rule, 0, *node_, at(milliseconds(40)))));
}

/** @brief Tests that Fail returns InvalidState naming the rule, else its position, and target. */
TEST_F(Small_RulePlacement, FailNamesTheRuleAndTheTarget)
{
    PlacementRule rule = remote_rule(AbsentAction::Fail);
    const PlacementDecision named = placement_.decide(rule, 2, *node_, at(milliseconds(0)));
    ASSERT_TRUE(std::holds_alternative<FailRun>(named));
    const Error& error = std::get<FailRun>(named).error;
    EXPECT_EQ(error.code, ErrorCode::InvalidState);
    EXPECT_NE(error.message.find("\"pin\""), std::string::npos) << error.message;
    EXPECT_NE(error.message.find("machine \"rack-7\""), std::string::npos) << error.message;

    rule.name.clear();
    const PlacementDecision unnamed = placement_.decide(rule, 2, *node_, at(milliseconds(0)));
    ASSERT_TRUE(std::holds_alternative<FailRun>(unnamed));
    EXPECT_NE(std::get<FailRun>(unnamed).error.message.find("at position 2"), std::string::npos);
}

/** @brief Tests that a device-only rule names the master only when the master has the device. */
TEST(Small_RulePlacementDevice, Regression_ADeviceOnlyRuleTheMasterLacksIsNotTheMasters)
{
    WorkerDirectory directory{span<const std::uint8_t>(),
                              std::make_shared<LeastLoadedOffloadPolicy>(0)};
    HeldNodes held;
    NodeFacts master{"master", OperatingSystem::Unknown, {},
                     {DeviceFacts{DeviceKind::CPU, "Test CPU", 1ull << 30}}};
    RulePlacement placement{directory, held, master};

    PlacementRule gpu;
    gpu.name = "gpu";
    gpu.match.placement_class = "test.pinned";
    gpu.target.device_kind = DeviceKind::GPU;
    EXPECT_FALSE(placement.names_master(gpu));

    PlacementRule second = gpu;
    second.target.device_kind.reset();
    second.target.device_index = 1;
    EXPECT_FALSE(placement.names_master(second));

    PlacementRule cpu = gpu;
    cpu.target.device_kind = DeviceKind::CPU;
    EXPECT_TRUE(placement.names_master(cpu));
}

/** @brief Tests that a Wait bound too large to add to the first offer saturates. */
TEST_F(Small_RulePlacement, Regression_AHugeWaitBoundWaitsUntilTheLatestTime)
{
    PlacementRule rule = remote_rule(AbsentAction::Wait);
    rule.if_absent.bound = milliseconds::max();
    const PlacementDecision decision = placement_.decide(rule, 0, *node_, at(milliseconds(0)));
    ASSERT_TRUE(std::holds_alternative<WaitForTarget>(decision));
    EXPECT_EQ(std::get<WaitForTarget>(decision).until,
              std::chrono::steady_clock::time_point::max());
}

/** @brief Tests that Fail refuses a node that cannot travel with an error naming the cause. */
TEST_F(Small_RulePlacement, FailRefusesANodeThatCannotTravel)
{
    const PlacementRule rule = remote_rule(AbsentAction::Fail);
    const PlacementDecision shared = placement_.decide_unreachable(
        rule, 0, *node_, UnreachableCause::NotShared);
    ASSERT_TRUE(std::holds_alternative<FailRun>(shared));
    const Error& error = std::get<FailRun>(shared).error;
    EXPECT_EQ(error.code, ErrorCode::InvalidArgument);
    EXPECT_NE(error.message.find("\"pin\""), std::string::npos) << error.message;
    EXPECT_NE(error.message.find("not shared"), std::string::npos) << error.message;

    const PlacementDecision spent = placement_.decide_unreachable(
        rule, 0, *node_, UnreachableCause::OutOfAttempts);
    ASSERT_TRUE(std::holds_alternative<FailRun>(spent));
    EXPECT_EQ(std::get<FailRun>(spent).error.code, ErrorCode::InvalidState);
}

/** @brief Tests that Local and Wait run a node that cannot travel on the master at once. */
TEST_F(Small_RulePlacement, LocalAndWaitRunANodeThatCannotTravelOnTheMaster)
{
    for (const AbsentAction action : {AbsentAction::Local, AbsentAction::Wait})
        EXPECT_TRUE(std::holds_alternative<RunLocally>(placement_.decide_unreachable(
            remote_rule(action), 0, *node_, UnreachableCause::Untagged)));
    EXPECT_EQ(held_.size(), 0u);
}
