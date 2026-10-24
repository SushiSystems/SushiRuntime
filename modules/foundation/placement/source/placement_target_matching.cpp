/**************************************************************************/
/* placement_target_matching.cpp                                          */
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
 * @file placement_target_matching.cpp
 * @brief Implements the match of a rule's target against the facts of one machine.
 * @author Mustafa Garip
 * @see modules/foundation/placement/README.md
 */

#include <SushiRuntime/placement/placement_target_matching.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_label.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Returns the name of a device kind for a message. */
        [[nodiscard]] const char* kind_name(DeviceKind kind) noexcept
        {
            switch (kind)
            {
                case DeviceKind::CPU:
                    return "CPU";
                case DeviceKind::GPU:
                    return "GPU";
                case DeviceKind::Accelerator:
                    return "Accelerator";
                case DeviceKind::Other:
                    return "Other";
            }
            return "Other";
        }

        /** @brief Returns the first DNS label of a machine name: the text before the first '.'. */
        [[nodiscard]] std::string_view first_label(std::string_view name) noexcept
        {
            return name.substr(0, name.find('.'));
        }

        /** @brief Returns a character in ASCII lower case; other bytes are kept. */
        [[nodiscard]] char lower_ascii(char character) noexcept
        {
            if (character >= 'A' && character <= 'Z')
                return static_cast<char>(character - 'A' + 'a');
            return character;
        }

        /** @brief Returns the first device of a kind, if any. */
        [[nodiscard]] std::optional<std::size_t> first_of_kind(const NodeFacts& machine,
                                                               DeviceKind kind) noexcept
        {
            for (std::size_t index = 0; index < machine.devices.size(); ++index)
            {
                if (machine.devices[index].kind == kind)
                    return index;
            }
            return std::nullopt;
        }

        /** @brief Returns the first device that holds at least a given memory, if any. */
        [[nodiscard]] std::optional<std::size_t> first_with_memory(const NodeFacts& machine,
                                                                   std::uint64_t memory) noexcept
        {
            for (std::size_t index = 0; index < machine.devices.size(); ++index)
            {
                if (machine.devices[index].memory_bytes >= memory)
                    return index;
            }
            return std::nullopt;
        }
    } // namespace

    bool machine_names_match(std::string_view first, std::string_view second) noexcept
    {
        const std::string_view left = first_label(first);
        const std::string_view right = first_label(second);
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (lower_ascii(left[index]) != lower_ascii(right[index]))
                return false;
        }
        return true;
    }

    bool machine_matches(const PlacementTarget& target, const NodeFacts& machine) noexcept
    {
        if (!target.machine_name.empty() &&
            !machine_names_match(target.machine_name, machine.name))
            return false;
        if (!target.machine_label)
            return true;
        for (const NodeLabel& label : machine.labels)
        {
            if (label.key == target.machine_label->key &&
                label.value == target.machine_label->value)
                return true;
        }
        return false;
    }

    std::optional<std::size_t> pick_device(const PlacementTarget& target,
                                           const NodeFacts& machine) noexcept
    {
        if (target.device_index)
        {
            if (*target.device_index < machine.devices.size())
                return *target.device_index;
            return std::nullopt;
        }
        if (target.device_kind)
            return first_of_kind(machine, *target.device_kind);
        if (target.device_min_memory)
            return first_with_memory(machine, *target.device_min_memory);
        return K_DEFAULT_PLACEMENT_DEVICE;
    }

    std::optional<std::size_t> device_on(const PlacementTarget& target,
                                         const NodeFacts& machine) noexcept
    {
        if (!machine_matches(target, machine))
            return std::nullopt;
        return pick_device(target, machine);
    }

    std::string describe_rule(const PlacementRule& rule, std::size_t position)
    {
        if (!rule.name.empty())
            return "\"" + rule.name + "\"";
        return "at position " + std::to_string(position);
    }

    std::string describe_target(const PlacementTarget& target)
    {
        std::string text;
        const auto append = [&text](const std::string& part)
        {
            if (!text.empty())
                text += ", ";
            text += part;
        };
        if (!target.machine_name.empty())
            append("machine \"" + target.machine_name + "\"");
        if (target.machine_label)
            append("machine label " + target.machine_label->key + "=" +
                   target.machine_label->value);
        if (target.device_index)
            append("device index " + std::to_string(*target.device_index));
        if (target.device_kind)
            append(std::string("the first device of kind ") + kind_name(*target.device_kind));
        if (target.device_min_memory)
            append("the first device with at least " +
                   std::to_string(*target.device_min_memory) + " bytes");
        return text;
    }
} // namespace SushiRuntime
