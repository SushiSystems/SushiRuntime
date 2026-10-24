/**************************************************************************/
/* rule_stamp.hpp                                                         */
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
 * @file rule_stamp.hpp
 * @brief Declares RuleStamp, which writes a node's winning rule and local device at compile.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>
#include <SushiRuntime/task/node.hpp>

#include "local_rule_device.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Writes onto each node of a compile the rule that wins for it and the local
         *        device that rule gives.
         * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
         */
        class RuleStamp final
        {
            public:
                /**
                 * @brief Holds what one compile reads; every pointer outlives the stamp.
                 * @param rules          The set in force; null stamps nothing.
                 * @param here           This machine's facts; null names no local device.
                 * @param settles_absent Whether an absent target's if_absent is applied here,
                 *                       which holds when no offload coordinator is installed.
                 */
                RuleStamp(const PlacementRules* rules, const NodeFacts* here,
                          bool settles_absent) noexcept
                    : rules_(rules), here_(here), settles_absent_(settles_absent)
                {
                }

                /**
                 * @brief Stamps one node; a device the node states itself is kept.
                 * @return Nothing, or the refusal of a Fail rule whose target is absent when
                 *         this compile settles absent targets.
                 */
                [[nodiscard]] Result<void> stamp(Internal::Graph::Node& node) const
                {
                    if (rules_ == nullptr)
                        return {};
                    Internal::Graph::TaskMetadata& metadata = node.metadata;
                    const std::optional<std::size_t> winner = rules_->resolve(
                        metadata.placement_class, metadata.placement_key, metadata.op_id);
                    if (!winner)
                        return {};
                    metadata.placement_rule = static_cast<std::uint32_t>(*winner);
                    if (here_ == nullptr)
                        return {};

                    const PlacementRule& rule = rules_->rule(*winner);
                    const std::optional<std::size_t> device = device_on(rule.target, *here_);
                    if (!device && settles_absent_ && rule.if_absent.action == AbsentAction::Fail)
                    {
                        const char* name = metadata.name != nullptr ? metadata.name : "";
                        return make_error(absent_refusal(rule, *winner, *here_, "compile",
                                                         "node \"" + std::string(name) + "\""));
                    }
                    if (device && !node.stated_placement())
                        node.place_on(*device);
                    return {};
                }

            private:
                /// @brief The rule set in force; null when there is none.
                const PlacementRules* rules_;

                /// @brief This machine's facts; null when the graph was built without them.
                const NodeFacts* here_;

                /// @brief Whether an absent target's if_absent is applied at compile.
                bool settles_absent_;
        };
    } // namespace Detail
} // namespace SushiRuntime
