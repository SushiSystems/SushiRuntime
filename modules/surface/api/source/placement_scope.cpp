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
 * @file placement_scope.cpp
 * @brief Implements the device a PlacementScope resolves to against the rules in force.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#include <SushiRuntime/api/runtime/placement_scope.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>

#include <SushiRuntime/api/detail/local_rule_device.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds the device a scope takes when no rule or no device part decides. */
        constexpr std::size_t K_DEFAULT_DEVICE = K_DEFAULT_PLACEMENT_DEVICE;
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
        if (!context_)
            return DeviceIndex{K_DEFAULT_DEVICE};
        std::optional<Error> refusal;
        std::size_t chosen = K_DEFAULT_DEVICE;
        Result<void> translated = Detail::translate_exceptions(
            [this, &refusal, &chosen]()
            {
                const Internal::Execution::RuntimeContext& runtime =
                    Internal::ContextAccess::context(context_);
                const NodeFacts* here = runtime.machine_facts();
                const auto placement = runtime.placement();
                if (here == nullptr || !placement->rules)
                    return;
                Result<std::size_t> resolved = Detail::resolve_scope_device(placement_class(), placement_key(), *placement->rules, *here);
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
