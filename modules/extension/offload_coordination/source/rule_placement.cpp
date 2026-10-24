/**************************************************************************/
/* rule_placement.cpp                                                     */
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
 * @file rule_placement.cpp
 * @brief Implements RulePlacement: the master, a named worker, or the rule's if_absent.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/rule_placement.hpp>

#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/placement/placement_target_matching.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns the error of a Fail rule whose target is absent. */
            [[nodiscard]] Error absent_target_error(const PlacementRule& rule,
                                                    std::size_t position,
                                                    const Internal::Graph::Node& node)
            {
                const char* name = node.metadata.name != nullptr ? node.metadata.name : "";
                return Error{ErrorCode::InvalidState,
                             "placement rule " + describe_rule(rule, position) +
                                 " places node \"" + name + "\" on " +
                                 describe_target(rule.target) +
                                 ", which no alive worker with the node's kernel matches, and "
                                 "the rule's if_absent is Fail. Start that worker, or set "
                                 "if_absent to Local or Wait."};
            }
        } // namespace

        RulePlacement::RulePlacement(WorkerDirectory& directory, HeldNodes& held,
                                     NodeFacts master)
            : directory_(directory), held_(held), master_(std::move(master))
        {

        }

        bool RulePlacement::names_master(const PlacementRule& rule) const noexcept
        {
            return machine_matches(rule.target, master_);
        }

        PlacementDecision RulePlacement::decide(const PlacementRule& rule, std::size_t position,
                                                const Internal::Graph::Node& node,
                                                std::chrono::steady_clock::time_point now)
        {
            if (names_master(rule))
                return RunLocally{};

            const TargetPick pick = directory_.pick_target(rule.target, node.metadata.op_id);
            if (pick.status == TargetStatus::Picked)
                return RunOnWorker{static_cast<int>(pick.worker.value), pick.device_index,
                                   rule.deadline};

            // A busy target is present: the node waits for its slot, whatever if_absent says.
            if (pick.status == TargetStatus::Busy)
                return WaitForTarget{std::chrono::steady_clock::time_point::max()};
            return follow_if_absent(rule, position, node, now);
        }

        PlacementDecision RulePlacement::follow_if_absent(const PlacementRule& rule,
                                                          std::size_t position,
                                                          const Internal::Graph::Node& node,
                                                          std::chrono::steady_clock::time_point now)
        {
            switch (rule.if_absent.action)
            {
                case AbsentAction::Local:
                    return RunLocally{};
                case AbsentAction::Wait:
                {
                    const std::chrono::steady_clock::time_point until =
                        held_.first_offer(node, now) + rule.if_absent.bound;
                    if (now < until)
                        return WaitForTarget{until};
                    return RunLocally{};
                }
                case AbsentAction::Fail:
                    break;
            }
            return FailRun{absent_target_error(rule, position, node)};
        }

    } // namespace Distributed
} // namespace SushiRuntime
