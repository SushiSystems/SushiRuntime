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
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/partition_session.hpp>

#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/partition_session/partition_session_log_category.hpp>
#include <SushiRuntime/partitioning/assignment_hash.hpp>
#include <SushiRuntime/partitioning/partition_report_builder.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the journal reason of a horizon's action other than Keep. */
            [[nodiscard]] PlacementReason reason_of(RebalanceAction action) noexcept
            {
                switch (action)
                {
                    case RebalanceAction::Replan:
                        return PlacementReason::Replan;
                    case RebalanceAction::TryFloor:
                        return PlacementReason::TryFloor;
                    case RebalanceAction::KeepFloor:
                        return PlacementReason::KeepFloor;
                    case RebalanceAction::LeaveFloor:
                        return PlacementReason::LeaveFloor;
                    case RebalanceAction::Keep:
                        break;
                }
                return PlacementReason::First;
            }
        } // namespace

        PartitionSession::PartitionSession(PartitionSessionParts parts, std::unique_ptr<PlanSource> source)
            : parts_(std::move(parts)), source_(std::move(source)), journal_(parts_.journal_file)
        {
            SR_VERIFY(parts_.copies != nullptr, "PartitionSession: pass the tally of the runtime's copies, not null.");
            SR_VERIFY(source_ != nullptr, "PartitionSession: pass the source of the session's plans, not null.");
        }

        const Result<PartitionAssignment, PartitionRefusal>& PartitionSession::partition(PartitionProblem problem)
        {
            Result<PartitionPlan> plan = source_->first(std::move(problem));
            if (!plan)
            {
                refusal_.emplace(make_error(PartitionRefusal{std::move(plan.error()), {}}));
                return *refusal_;
            }
            install(std::move(plan.value()));
            journal_plan(PlacementReason::First, PartitionReport{});
            return in_force_->answer;
        }

        const Result<PartitionAssignment, PartitionRefusal>& PartitionSession::answer() const
        {
            SR_VERIFY(in_force_.has_value(), "PartitionSession::answer(): no plan is in force; check has_plan().");
            return in_force_->answer;
        }

        bool PartitionSession::measure(std::uint64_t problem_hash, std::size_t steps, double seconds,
                                       const std::vector<DispatchObservation>& dispatches)
        {
            if (!in_force_ || in_force_->problem_hash != problem_hash)
            {
                ++dropped_runs_;
                return false;
            }
            ledger_.add_steps(steps, seconds);
            if (!dispatches.empty())
                ledger_.add_dispatches(dispatches);
            return true;
        }

        RebalanceAction PartitionSession::rebalance()
        {
            const std::vector<DispatchObservation> observed = ledger_.take_horizon();
            if (!in_force_ || !in_force_->answer)
                return RebalanceAction::Keep;
            ++horizons_;
            const PartitionReport judged = report();
            HorizonOutcome outcome = source_->horizon(*in_force_, judged, observed, horizons_);
            if (outcome.action == RebalanceAction::Keep)
                return outcome.action;
            if (outcome.plan)
                install(std::move(*outcome.plan));
            journal_plan(reason_of(outcome.action), judged);
            return outcome.action;
        }

        PathTallies PartitionSession::copies_since_install() const
        {
            PathTallies since = parts_.copies->tallies();
            for (auto& entry : since)
            {
                const auto before = copies_at_install_.find(entry.first);
                if (before == copies_at_install_.end())
                    continue;
                entry.second.copies -= before->second.copies;
                entry.second.bytes -= before->second.bytes;
            }
            return since;
        }

        PartitionReport PartitionSession::report() const
        {
            if (!in_force_ || in_force_->model == nullptr)
                return PartitionReport{};
            return make_partition_report(in_force_->problem, *in_force_->model, in_force_->answer,
                                         ledger_.measurement(copies_since_install(), parts_.device_of));
        }

        void PartitionSession::install(PartitionPlan plan)
        {
            in_force_.reset();
            in_force_.emplace(std::move(plan));
            ledger_.clear();
            // The tally counts every graph's copies, so a session reads the difference and clears nothing.
            copies_at_install_ = parts_.copies->tallies();
            ++generation_;
        }

        void PartitionSession::journal_plan(PlacementReason reason, const PartitionReport& judged)
        {
            PlacementJournalEntry entry;
            entry.reason = reason;
            entry.planned_by = in_force_->planned_by;
            entry.horizon = horizons_;
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
                SR_LOG_WARN(K_PARTITION_SESSION_LOG_CATEGORY,
                            "A partition decision was kept in memory but not written to the placement journal: {}.",
                            appended.error().message);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
