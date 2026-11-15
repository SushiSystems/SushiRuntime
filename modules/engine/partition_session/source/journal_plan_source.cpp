/**************************************************************************/
/* journal_plan_source.cpp                                                */
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
 * @file journal_plan_source.cpp
 * @brief Implements JournalPlanSource.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/journal_plan_source.hpp>

#include <memory>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/partition_session/journal_replay.hpp>
#include <SushiRuntime/partition_session/partition_session_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the action a horizon reports for an entry of reason. */
            [[nodiscard]] RebalanceAction action_of(PlacementReason reason) noexcept
            {
                switch (reason)
                {
                    case PlacementReason::Replan:
                        return RebalanceAction::Replan;
                    case PlacementReason::TryFloor:
                        return RebalanceAction::TryFloor;
                    case PlacementReason::KeepFloor:
                        return RebalanceAction::KeepFloor;
                    case PlacementReason::LeaveFloor:
                        return RebalanceAction::LeaveFloor;
                    case PlacementReason::First:
                        break;
                }
                return RebalanceAction::Keep;
            }
        } // namespace

        JournalPlanSource::JournalPlanSource(std::vector<PlacementJournalEntry> entries,
                                             PartitionerFactories partitioners)
            : entries_(std::move(entries)), partitioners_(std::move(partitioners))
        {
        }

        Result<PartitionPlan> JournalPlanSource::first(PartitionProblem problem)
        {
            if (next_ >= entries_.size())
                return make_error(Error{ErrorCode::InvalidState, "JournalPlanSource: the placement journal holds no entry " +
                                                                     std::to_string(next_) +
                                                                     " to answer a partition with"});
            const PlacementJournalEntry& entry = entries_[next_];
            if (entry.reason != PlacementReason::First)
                return make_error(Error{ErrorCode::InvalidState, "JournalPlanSource: entry " +
                                                                     std::to_string(entry.sequence) +
                                                                     " of the placement journal is not a first partition"});
            Result<PartitionPlan> plan = replay(entry, std::move(problem));
            if (plan)
                ++next_;
            return plan;
        }

        HorizonOutcome JournalPlanSource::horizon(const PartitionPlan& in_force, const PartitionReport&,
                                                  const std::vector<DispatchObservation>&, std::uint64_t horizon)
        {
            if (next_ >= entries_.size() || entries_[next_].reason == PlacementReason::First)
                return HorizonOutcome{};
            const PlacementJournalEntry& entry = entries_[next_];
            // An entry of a version 1 journal carries no horizon and applies at the next one.
            if (entry.horizon && *entry.horizon != horizon)
                return HorizonOutcome{};
            ++next_;
            if (entry.reason == PlacementReason::KeepFloor)
                return HorizonOutcome{RebalanceAction::KeepFloor, std::nullopt};
            Result<PartitionPlan> plan = replay(entry, in_force.problem);
            if (!plan)
            {
                SR_LOG_ERROR(K_PARTITION_SESSION_LOG_CATEGORY,
                             "The placement journal could not be replayed at a horizon, so the plan in force stays: "
                             "{}.",
                             plan.error().message);
                return HorizonOutcome{};
            }
            return HorizonOutcome{action_of(entry.reason), std::move(plan.value())};
        }

        Result<PartitionPlan> JournalPlanSource::replay(const PlacementJournalEntry& entry, PartitionProblem problem) const
        {
            Result<PartitionAssignment> answer = replay_placement(entry, problem, partitioners_);
            if (!answer)
                return make_error(std::move(answer.error()));
            return PartitionPlan{std::move(problem), entry.problem_hash, std::move(answer.value()),
                                 std::make_shared<const CostModel>(entry.profiles), entry.planned_by};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
