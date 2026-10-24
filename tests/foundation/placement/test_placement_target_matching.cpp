/**************************************************************************/
/* test_placement_target_matching.cpp                                     */
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
 * @file test_placement_target_matching.cpp
 * @brief Tests the match of a target's machine part and device part against a machine.
 * @author Mustafa Garip
 * @see tests/foundation/placement/README.md
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/node_label.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Holds the memory of the test machine's CPU. */
    constexpr std::uint64_t K_CPU_MEMORY = 32ull << 30;

    /** @brief Holds the memory of the test machine's GPU. */
    constexpr std::uint64_t K_GPU_MEMORY = 8ull << 30;

    /** @brief Returns a machine named "rack-7" in zone "a", with a CPU then a GPU. */
    NodeFacts rack_seven()
    {
        NodeFacts machine;
        machine.name = "rack-7";
        machine.labels = {NodeLabel{"zone", "a"}};
        machine.devices = {DeviceFacts{DeviceKind::CPU, "Test CPU", K_CPU_MEMORY},
                           DeviceFacts{DeviceKind::GPU, "Test GPU", K_GPU_MEMORY}};
        return machine;
    }
} // namespace

/** @brief Tests that a name or a label matches the machine it names and no other. */
TEST(Small_PlacementTargetMatching, MachinePartMatchesByNameOrLabel)
{
    const NodeFacts machine = rack_seven();
    PlacementTarget target;
    EXPECT_TRUE(machine_matches(target, machine)) << "a target with no machine part";

    target.machine_name = "rack-7";
    EXPECT_TRUE(machine_matches(target, machine));
    target.machine_name = "rack-8";
    EXPECT_FALSE(machine_matches(target, machine));

    target.machine_name.clear();
    target.machine_label = NodeLabel{"zone", "a"};
    EXPECT_TRUE(machine_matches(target, machine));
    target.machine_label = NodeLabel{"zone", "b"};
    EXPECT_FALSE(machine_matches(target, machine));
}

/** @brief Tests that each device part picks its device, and none when the machine lacks it. */
TEST(Small_PlacementTargetMatching, DevicePartPicksByIndexKindOrMemory)
{
    const NodeFacts machine = rack_seven();
    PlacementTarget target;
    EXPECT_EQ(pick_device(target, machine), std::optional<std::size_t>(0));

    target.device_index = 1;
    EXPECT_EQ(pick_device(target, machine), std::optional<std::size_t>(1));
    target.device_index = 2;
    EXPECT_EQ(pick_device(target, machine), std::nullopt);

    target.device_index.reset();
    target.device_kind = DeviceKind::GPU;
    EXPECT_EQ(pick_device(target, machine), std::optional<std::size_t>(1));
    target.device_kind = DeviceKind::Accelerator;
    EXPECT_EQ(pick_device(target, machine), std::nullopt);

    target.device_kind.reset();
    target.device_min_memory = K_GPU_MEMORY;
    EXPECT_EQ(pick_device(target, machine), std::optional<std::size_t>(0));
    target.device_min_memory = K_CPU_MEMORY + 1;
    EXPECT_EQ(pick_device(target, machine), std::nullopt);
}

/** @brief Tests that the phrases name a rule by name, else by position, and every target part. */
TEST(Small_PlacementTargetMatching, DescriptionsNameTheRuleAndEveryTargetPart)
{
    PlacementRule rule;
    EXPECT_EQ(describe_rule(rule, 3), "at position 3");
    rule.name = "pin";
    EXPECT_EQ(describe_rule(rule, 3), "\"pin\"");

    PlacementTarget target;
    target.machine_name = "rack-7";
    target.device_kind = DeviceKind::GPU;
    EXPECT_EQ(describe_target(target), "machine \"rack-7\", the first device of kind GPU");
}
