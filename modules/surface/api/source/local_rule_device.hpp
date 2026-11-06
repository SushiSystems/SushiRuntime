/**************************************************************************/
/* local_rule_device.hpp                                                  */
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
 * @file local_rule_device.hpp
 * @brief Declares the local device a placement rule gives on this machine, after if_absent.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns the refusal of a Fail rule whose target this machine lacks.
         * @param what Names what is placed, such as "the scope".
         */
        [[nodiscard]] inline Error absent_refusal(const PlacementRule& rule, std::size_t position,
                                                  const NodeFacts& here, const char* caller,
                                                  const std::string& what)
        {
            return Error{ErrorCode::InvalidArgument,
                         std::string(caller) + ": rule " + describe_rule(rule, position) +
                             " places " + what + " on " + describe_target(rule.target) +
                             ", which machine \"" + here.name + "\" does not have (it has " +
                             std::to_string(here.devices.size()) +
                             " device(s)), and the rule's if_absent is Fail. Set if_absent to "
                             "Local or Wait, or name a device this machine has."};
        }

        /**
         * @brief Returns the local device a rule gives here, by device_on() then if_absent.
         *
         * Local and Wait give none at once, since a local device never appears during a run;
         * Fail gives absent_refusal().
         * @return The device, or none when the caller places as it would with no rule.
         */
        [[nodiscard]] inline Result<std::optional<std::size_t>>
        local_device_under(const PlacementRule& rule, std::size_t position, const NodeFacts& here,
                           const char* caller, const std::string& what)
        {
            const std::optional<std::size_t> device = device_on(rule.target, here);
            if (device || rule.if_absent.action != AbsentAction::Fail)
                return device;
            return make_error(absent_refusal(rule, position, here, caller, what));
        }
    } // namespace Detail
} // namespace SushiRuntime
