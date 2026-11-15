/**************************************************************************/
/* partition_session.cpp                                                  */
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
 * @file partition_session.cpp
 * @brief Implements PartitionSession.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/partition_session.hpp>

#include <utility>

#include <SushiRuntime/coherence/assignment_hash.hpp>
#include <SushiRuntime/coherence/coherence_log_category.hpp>
#include <SushiRuntime/coherence/partition_report_builder.hpp>
#include <SushiRuntime/coherence/problem_hash.hpp>
#include <SushiRuntime/coherence/profile_fold.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/log/logger.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PartitionSession::PartitionSession(PartitionSessionParts parts)
            : parts_(std::move(parts)), model_(parts_.model), journal_(parts_.journal_file)
        {
            SR_VERIFY(parts_.copies != nullptr, "PartitionSession: pass the tally of the runtime's copies, not null.");
            if (model_ != nullptr && parts_.partitioners.scheduler)
                scheduler_ = parts_.partitioners.scheduler(model_);
        }

        const Result<PartitionAssignment, PartitionRefusal>& PartitionSession::partition(PartitionProblem problem)
        {
            SR_VERIFY(is_active(), "PartitionSession::partition(): the session has no partitioner; check is_active().");
            Result<PartitionAssignment, PartitionRefusal> answer = scheduler_->partition(problem);
            const std::uint64_t hash = Internal::Coherence::problem_hash(problem);
            install(Plan{std::move(problem), hash, std::move(answer), model_, PlannedBy::Scheduler});
            replaced_.reset();
            state_ = RebalanceState{};
            journal_plan(PlacementReason::First, PartitionReport{});
            return in_force_->answer;
        }

        const Result<PartitionAssignment, PartitionRefusal>& PartitionSession::answer() const
        {
            SR_VERIFY(in_force_.has_value(), "PartitionSession::answer(): no plan is in force; check has_plan().");
            return in_force_->answer;
        }

        RebalanceAction PartitionSession::rebalance()
        {
            const std::vector<DispatchObservation> observed = ledger_.take_horizon();
            if (!is_active() || !in_force_ || !in_force_->answer)
                return RebalanceAction::Keep;
            const PartitionReport judged = report();
            if (!observed.empty())
            {
                model_ = std::make_shared<const CostModel>(fold_observations(model_->profiles(), observed));
                scheduler_ = parts_.partitioners.scheduler(model_);
            }

            const RebalanceAction action = decide_rebalance(judged, parts_.margin, state_);
            switch (action)
            {
                case RebalanceAction::Keep:
                    return action;
                case RebalanceAction::Replan:
                {
                    Result<PartitionAssignment, PartitionRefusal> answer = scheduler_->partition(in_force_->problem);
                    if (!answer)
                        return RebalanceAction::Keep;
                    install(Plan{in_force_->problem, in_force_->problem_hash, std::move(answer), model_,
                                 PlannedBy::Scheduler});
                    replaced_.reset();
                    state_ = RebalanceState{};
                    journal_plan(PlacementReason::Replan, judged);
                    return action;
                }
                case RebalanceAction::TryFloor:
                {
                    if (!parts_.partitioners.floor)
                        return RebalanceAction::Keep;
                    Result<PartitionAssignment, PartitionRefusal> floor =
                        parts_.partitioners.floor(model_)->partition(in_force_->problem);
                    if (!floor)
                        return RebalanceAction::Keep;
                    Plan trial{in_force_->problem, in_force_->problem_hash, std::move(floor), model_, PlannedBy::Floor};
                    replaced_ = std::move(in_force_);
                    install(std::move(trial));
                    state_ = RebalanceState{true, judged.measured_step_seconds, false};
                    journal_plan(PlacementReason::TryFloor, judged);
                    return action;
                }
                case RebalanceAction::KeepFloor:
                    replaced_.reset();
                    state_ = RebalanceState{false, 0.0, true};
                    journal_plan(PlacementReason::KeepFloor, judged);
                    return action;
                case RebalanceAction::LeaveFloor:
                {
                    Plan restored = std::move(*replaced_);
                    replaced_.reset();
                    install(std::move(restored));
                    state_ = RebalanceState{false, 0.0, true};
                    journal_plan(PlacementReason::LeaveFloor, judged);
                    return action;
                }
            }
            return RebalanceAction::Keep;
        }

        PartitionReport PartitionSession::report() const
        {
            if (!in_force_ || in_force_->model == nullptr)
                return PartitionReport{};
            return make_partition_report(in_force_->problem, *in_force_->model, in_force_->answer,
                                         ledger_.measurement(parts_.copies->tallies(), parts_.device_of));
        }

        void PartitionSession::install(Plan plan)
        {
            in_force_.reset();
            in_force_.emplace(std::move(plan));
            ledger_.clear();
            parts_.copies->clear();
            ++generation_;
        }

        void PartitionSession::journal_plan(PlacementReason reason, const PartitionReport& judged)
        {
            PlacementJournalEntry entry;
            entry.reason = reason;
            entry.planned_by = in_force_->planned_by;
            entry.problem_hash = in_force_->problem_hash;
            entry.assignment_hash = in_force_->answer ? assignment_hash(in_force_->answer.value()) : 0;
            entry.modelled_step_seconds = judged.step_seconds;
            entry.measured_step_seconds = judged.measured_step_seconds;
            entry.floor_step_seconds = judged.floor_step_seconds;
            for (const PartitionDeviceRow& row : judged.rows)
                entry.measured_device_seconds.emplace(row.device, row.measured_seconds);
            entry.profiles = in_force_->model->profiles();
            const Result<void> appended = journal_.append(std::move(entry));
            if (!appended)
                SR_LOG_WARN(K_COHERENCE_LOG_CATEGORY,
                            "A partition decision was kept in memory but not written to the placement journal: {}.",
                            appended.error().message);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
