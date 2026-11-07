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
 * @file test_placement_target_matching.cpp
 * @brief Tests the match of a target's machine part and device part against a machine.
 * @author Mustafa Garip
 * @see tests/foundation/placement/README.md
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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

    /** @brief Returns a target that names a machine. */
    PlacementTarget named(const char* machine_name)
    {
        PlacementTarget target;
        target.machine_name = machine_name;
        return target;
    }

    /** @brief Returns a target that asks for a machine label. */
    PlacementTarget labelled(const char* key, const char* value)
    {
        PlacementTarget target;
        target.machine_label = NodeLabel{key, value};
        return target;
    }

    /** @brief Returns a target that asks for the device at an index. */
    PlacementTarget at_index(std::size_t index)
    {
        PlacementTarget target;
        target.device_index = index;
        return target;
    }

    /** @brief Returns a target that asks for the first device of a kind. */
    PlacementTarget of_kind(DeviceKind kind)
    {
        PlacementTarget target;
        target.device_kind = kind;
        return target;
    }

    /** @brief Returns a target that asks for the first device with at least the given memory. */
    PlacementTarget with_memory(std::uint64_t bytes)
    {
        PlacementTarget target;
        target.device_min_memory = bytes;
        return target;
    }

    /** @brief Holds a target and whether it matches the machine under test. */
    struct MachineCase
    {
        const char* label;      /**< Names the case in a failure. */
        PlacementTarget target; /**< The target under test. */
        bool expect;            /**< Whether the machine part matches. */
    };

    /** @brief Holds a target and the device pick_device is expected to give. */
    struct PickCase
    {
        const char* label;                 /**< Names the case in a failure. */
        PlacementTarget target;            /**< The target under test. */
        std::optional<std::size_t> expect; /**< The expected device position. */
    };

    /** @brief Holds two machine names and whether they match. */
    struct NameCase
    {
        const char* label;  /**< Names the case in a failure. */
        const char* wanted; /**< The name a target asks for. */
        const char* actual; /**< The name a machine carries. */
        bool expect;        /**< Whether the names match. */
    };

    /** @brief Expects machine_matches to give each case's answer for @p machine. */
    void expect_machine_matches(const NodeFacts& machine, const std::vector<MachineCase>& cases)
    {
        for (const MachineCase& item : cases)
        {
            SCOPED_TRACE(item.label);
            EXPECT_EQ(machine_matches(item.target, machine), item.expect);
        }
    }

    /** @brief Expects pick_device to give each case's position for @p machine. */
    void expect_picks(const NodeFacts& machine, const std::vector<PickCase>& cases)
    {
        for (const PickCase& item : cases)
        {
            SCOPED_TRACE(item.label);
            EXPECT_EQ(pick_device(item.target, machine), item.expect);
        }
    }

    /** @brief Expects machine_names_match to give each case's answer. */
    void expect_names(const std::vector<NameCase>& cases)
    {
        for (const NameCase& item : cases)
        {
            SCOPED_TRACE(item.label);
            EXPECT_EQ(machine_names_match(item.wanted, item.actual), item.expect);
        }
    }
} // namespace

/** @brief Tests that a name or a label matches the machine it names and no other. */
TEST(Small_PlacementTargetMatching, MachinePartMatchesByNameOrLabel)
{
    expect_machine_matches(rack_seven(), {
        {"a target with no machine part", PlacementTarget(), true},
        {"the machine's name", named("rack-7"), true},
        {"another name", named("rack-8"), false},
        {"a label the machine carries", labelled("zone", "a"), true},
        {"a label with another value", labelled("zone", "b"), false},
    });
}

/** @brief Tests that a target with no device part picks the first device. */
TEST(Small_PlacementTargetMatching, NoDevicePartPicksTheFirstDevice)
{
    expect_picks(rack_seven(), {{"no device part", PlacementTarget(), 0}});
}

/** @brief Tests that an index picks that device, and none when the machine has fewer. */
TEST(Small_PlacementTargetMatching, DeviceIndexPicksThatDevice)
{
    expect_picks(rack_seven(), {
        {"index 1", at_index(1), 1},
        {"index past the devices", at_index(2), std::nullopt},
    });
}

/** @brief Tests that a kind picks the first device of that kind, and none when the machine has none. */
TEST(Small_PlacementTargetMatching, DeviceKindPicksTheFirstDeviceOfThatKind)
{
    expect_picks(rack_seven(), {
        {"a kind the machine has", of_kind(DeviceKind::GPU), 1},
        {"a kind the machine lacks", of_kind(DeviceKind::Accelerator), std::nullopt},
    });
}

/** @brief Tests that a least memory picks the first device with enough, and none when no device has it. */
TEST(Small_PlacementTargetMatching, DeviceMemoryPicksTheFirstDeviceWithEnough)
{
    expect_picks(rack_seven(), {
        {"memory the first device has", with_memory(K_GPU_MEMORY), 0},
        {"memory no device has", with_memory(K_CPU_MEMORY + 1), std::nullopt},
    });
}

/** @brief Tests that a rule is named by its name, else by its position. */
TEST(Small_PlacementTargetMatching, DescribeRuleNamesTheRuleByNameElseByPosition)
{
    PlacementRule rule;
    EXPECT_EQ(describe_rule(rule, 3), "at position 3");

    rule.name = "pin";
    EXPECT_EQ(describe_rule(rule, 3), "\"pin\"");
}

/** @brief Tests that a target is described by every part it states. */
TEST(Small_PlacementTargetMatching, DescribeTargetNamesEveryPartItStates)
{
    PlacementTarget target;
    target.machine_name = "rack-7";
    target.device_kind = DeviceKind::GPU;

    EXPECT_EQ(describe_target(target), "machine \"rack-7\", the first device of kind GPU");
}

/** @brief Tests that a device-only target names a machine only when the machine has the device. */
TEST(Small_PlacementTargetMatching, Regression_ADeviceOnlyTargetNamesOnlyAMachineWithTheDevice)
{
    const NodeFacts machine = rack_seven();
    NodeFacts cpu_only = machine;
    cpu_only.devices.pop_back();

    EXPECT_TRUE(machine_matches(of_kind(DeviceKind::GPU), cpu_only))
        << "the machine part alone names every machine";
    EXPECT_EQ(device_on(of_kind(DeviceKind::GPU), cpu_only), std::nullopt);
    EXPECT_EQ(device_on(of_kind(DeviceKind::GPU), machine), std::optional<std::size_t>(1));
    EXPECT_EQ(device_on(at_index(1), machine), std::optional<std::size_t>(1));
    EXPECT_EQ(device_on(at_index(1), cpu_only), std::nullopt);
}

/** @brief Tests that a device is named only when the machine part of the target matches too. */
TEST(Small_PlacementTargetMatching, Regression_ADeviceOnAMachineNeedsTheMachinePartToMatch)
{
    PlacementTarget second = at_index(1);
    second.machine_name = "rack-8";

    EXPECT_EQ(device_on(second, rack_seven()), std::nullopt);
}

/** @brief Tests that machine names compare in any case and by their first DNS label. */
TEST(Small_PlacementTargetMatching, Regression_MachineNamesCompareByCaseAndFirstLabel)
{
    expect_names({
        {"another case", "Rack-7", "rack-7", true},
        {"another case, a qualified name", "RACK-7", "rack-7.lab.example.com", true},
        {"two qualified names with one first label", "rack-7.lab", "rack-7.other", true},
        {"a longer first label", "rack-7", "rack-70", false},
        {"the name as a later label", "rack-7", "lab.rack-7", false},
    });
}

/** @brief Tests that machine_matches applies the same comparison to a target and a machine name. */
TEST(Small_PlacementTargetMatching, Regression_MachineMatchesComparesATargetNameLikeMachineNamesMatch)
{
    NodeFacts qualified = rack_seven();
    qualified.name = "rack-7.lab.example.com";

    EXPECT_TRUE(machine_matches(named("RACK-7.lab.example.com"), rack_seven()));
    EXPECT_TRUE(machine_matches(named("Rack-7"), qualified));
}
