/**************************************************************************/
/* placement_scope.cpp                                                    */
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
 * @file placement_scope.cpp
 * @brief Implements the device a PlacementScope resolves to against the rules in force.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#include <SushiRuntime/api/runtime/placement_scope.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/device_facts.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/node_label.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds the device a scope takes when no rule or no device part decides. */
        constexpr std::size_t K_DEFAULT_DEVICE = 0;

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

        /** @brief Returns the name of a rule, or its position when it has none. */
        [[nodiscard]] std::string rule_label(const PlacementRule& rule, std::size_t position)
        {
            if (!rule.name.empty())
                return "\"" + rule.name + "\"";
            return "at position " + std::to_string(position);
        }

        /** @brief Returns the target of a rule as a phrase for a message. */
        [[nodiscard]] std::string describe_target(const PlacementTarget& target)
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

        /** @brief Returns whether the machine part of a target names this machine. */
        [[nodiscard]] bool machine_matches(const PlacementTarget& target, const NodeFacts& here)
        {
            if (!target.machine_name.empty() && target.machine_name != here.name)
                return false;
            if (!target.machine_label)
                return true;
            for (const NodeLabel& label : here.labels)
            {
                if (label.key == target.machine_label->key &&
                    label.value == target.machine_label->value)
                    return true;
            }
            return false;
        }

        /** @brief Returns the first device that holds at least a given memory, if any. */
        [[nodiscard]] std::optional<std::size_t> first_with_memory(const NodeFacts& here,
                                                                   std::uint64_t memory)
        {
            for (std::size_t index = 0; index < here.devices.size(); ++index)
            {
                if (here.devices[index].memory_bytes >= memory)
                    return index;
            }
            return std::nullopt;
        }

        /** @brief Returns the first device of a kind, if any. */
        [[nodiscard]] std::optional<std::size_t> first_of_kind(const NodeFacts& here,
                                                               DeviceKind kind)
        {
            for (std::size_t index = 0; index < here.devices.size(); ++index)
            {
                if (here.devices[index].kind == kind)
                    return index;
            }
            return std::nullopt;
        }

        /**
         * @brief Returns the local device a target names, or none when this machine lacks it.
         *
         * A target with no device part names device 0.
         */
        [[nodiscard]] std::optional<std::size_t> local_device(const PlacementTarget& target,
                                                              const NodeFacts& here)
        {
            if (!machine_matches(target, here))
                return std::nullopt;
            if (target.device_index)
            {
                if (*target.device_index < here.devices.size())
                    return *target.device_index;
                return std::nullopt;
            }
            if (target.device_kind)
                return first_of_kind(here, *target.device_kind);
            if (target.device_min_memory)
                return first_with_memory(here, *target.device_min_memory);
            return K_DEFAULT_DEVICE;
        }

        /** @brief Returns the refusal of a rule whose target is absent and whose policy is Fail. */
        [[nodiscard]] Error absent_refusal(const PlacementRule& rule, std::size_t position,
                                           const NodeFacts& here)
        {
            return Error{ErrorCode::InvalidArgument,
                         "PlacementScope::device: rule " + rule_label(rule, position) +
                             " places the scope on " + describe_target(rule.target) +
                             ", which machine \"" + here.name + "\" does not have (it has " +
                             std::to_string(here.devices.size()) +
                             " device(s)), and the rule's if_absent is Fail. Set if_absent to "
                             "Local or Wait, or name a device this machine has."};
        }

        /**
         * @brief Applies the rule's if_absent policy to an absent target.
         * @return The device to use, or the refusal the policy raises.
         */
        [[nodiscard]] Result<std::size_t> follow_if_absent(const PlacementRule& rule,
                                                           std::size_t position,
                                                           const NodeFacts& here)
        {
            switch (rule.if_absent.action)
            {
                case AbsentAction::Local:
                    return K_DEFAULT_DEVICE;
                case AbsentAction::Wait:
                    // A local device never appears during a run, so the wait only spends the bound.
                    std::this_thread::sleep_for(rule.if_absent.bound);
                    return K_DEFAULT_DEVICE;
                case AbsentAction::Fail:
                    break;
            }
            return make_error(absent_refusal(rule, position, here));
        }

        /** @brief Resolves a class and a key against a rule set and this machine. */
        [[nodiscard]] Result<std::size_t> resolve_device(const PlacementScope& scope,
                                                         const PlacementRules& rules,
                                                         const NodeFacts& here)
        {
            const std::optional<std::size_t> position =
                rules.resolve(scope.placement_class(), scope.placement_key(), OperationID());
            if (!position)
                return K_DEFAULT_DEVICE;
            const PlacementRule& rule = rules.rule(*position);
            const std::optional<std::size_t> device = local_device(rule.target, here);
            if (device)
                return *device;
            return follow_if_absent(rule, *position, here);
        }
    } // namespace

    Error PlacementScope::missing_runtime(const char* call)
    {
        return Error{ErrorCode::InvalidState,
                     std::string(call) +
                         ": this scope was built without a runtime; build it with "
                         "Runtime::placement_scope()."};
    }

    Result<DeviceIndex> PlacementScope::device() const
    {
        if (!context_ || !topology_)
            return DeviceIndex{K_DEFAULT_DEVICE};
        std::optional<Error> refusal;
        std::size_t chosen = K_DEFAULT_DEVICE;
        Result<void> translated = Detail::translate_exceptions(
            [this, &refusal, &chosen]()
            {
                const std::shared_ptr<const PlacementRules>& rules =
                    Internal::ContextAccess::context(context_).placement_rules();
                if (!rules)
                    return;
                Result<std::size_t> resolved = resolve_device(*this, *rules, *topology_);
                if (!resolved)
                {
                    refusal = std::move(resolved.error());
                    return;
                }
                chosen = *resolved;
            });
        if (!translated)
            return make_error(std::move(translated.error()));
        if (refusal)
            return make_error(std::move(*refusal));
        return DeviceIndex{chosen};
    }
} // namespace SushiRuntime
