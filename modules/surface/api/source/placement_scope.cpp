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
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds the device a scope takes when no rule or no device part decides. */
        constexpr std::size_t K_DEFAULT_DEVICE = K_DEFAULT_PLACEMENT_DEVICE;

        /** @brief Returns the local device a target names, or none when this machine lacks it. */
        [[nodiscard]] std::optional<std::size_t> local_device(const PlacementTarget& target,
                                                              const NodeFacts& here) noexcept
        {
            if (!machine_matches(target, here))
                return std::nullopt;
            return pick_device(target, here);
        }

        /** @brief Returns the refusal of a rule whose target is absent and whose policy is Fail. */
        [[nodiscard]] Error absent_refusal(const PlacementRule& rule, std::size_t position,
                                           const NodeFacts& here)
        {
            return Error{ErrorCode::InvalidArgument,
                         "PlacementScope::device: rule " + describe_rule(rule, position) +
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
