/**************************************************************************/
/* live_plan_source.cpp                                                   */
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
 * @file live_plan_source.cpp
 * @brief Implements LivePlanSource.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/live_plan_source.hpp>

#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/partitioning/problem_hash.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        LivePlanSource::LivePlanSource(std::shared_ptr<ProfileStore> profiles, PartitionerFactories partitioners,
                                       double margin)
            : profiles_(std::move(profiles)), partitioners_(std::move(partitioners)), margin_(margin)
        {
            SR_VERIFY(profiles_ != nullptr && profiles_->model() != nullptr,
                      "LivePlanSource: pass a profile store that holds a model.");
            SR_VERIFY(static_cast<bool>(partitioners_.scheduler), "LivePlanSource: pass a scheduler factory.");
            refresh(profiles_->model());
        }

        void LivePlanSource::refresh(std::shared_ptr<const CostModel> model)
        {
            if (model == model_ && scheduler_ != nullptr)
                return;
            model_ = std::move(model);
            scheduler_ = partitioners_.scheduler(model_);
        }

        Result<PartitionPlan> LivePlanSource::first(PartitionProblem problem)
        {
            // Another graph's horizon may have folded the shared profiles since this source last planned.
            refresh(profiles_->model());
            Result<PartitionAssignment, PartitionRefusal> answer = scheduler_->partition(problem);
            const std::uint64_t hash = problem_hash(problem);
            replaced_.reset();
            state_ = RebalanceState{};
            return PartitionPlan{std::move(problem), hash, std::move(answer), model_, PlannedBy::Scheduler};
        }

        HorizonOutcome LivePlanSource::horizon(const PartitionPlan& in_force, const PartitionReport& judged,
                                               const std::vector<DispatchObservation>& observed, std::uint64_t)
        {
            refresh(profiles_->fold(observed));
            const RebalanceAction action = decide_rebalance(judged, margin_, state_);
            switch (action)
            {
                case RebalanceAction::Keep:
                    return HorizonOutcome{};
                case RebalanceAction::Replan:
                {
                    Result<PartitionAssignment, PartitionRefusal> answer = scheduler_->partition(in_force.problem);
                    if (!answer)
                        return HorizonOutcome{};
                    replaced_.reset();
                    state_ = RebalanceState{};
                    return HorizonOutcome{action, PartitionPlan{in_force.problem, in_force.problem_hash,
                                                                std::move(answer), model_, PlannedBy::Scheduler}};
                }
                case RebalanceAction::TryFloor:
                {
                    if (!partitioners_.floor)
                        return HorizonOutcome{};
                    Result<PartitionAssignment, PartitionRefusal> floor =
                        partitioners_.floor(model_)->partition(in_force.problem);
                    if (!floor)
                        return HorizonOutcome{};
                    replaced_ = in_force;
                    state_ = RebalanceState{true, judged.measured_step_seconds, false};
                    return HorizonOutcome{action, PartitionPlan{in_force.problem, in_force.problem_hash,
                                                                std::move(floor), model_, PlannedBy::Floor}};
                }
                case RebalanceAction::KeepFloor:
                    replaced_.reset();
                    state_ = RebalanceState{false, 0.0, true};
                    return HorizonOutcome{action, std::nullopt};
                case RebalanceAction::LeaveFloor:
                {
                    SR_VERIFY(replaced_.has_value(), "LivePlanSource: a floor left with no plan it replaced.");
                    PartitionPlan restored = std::move(*replaced_);
                    replaced_.reset();
                    state_ = RebalanceState{false, 0.0, true};
                    return HorizonOutcome{action, std::move(restored)};
                }
            }
            return HorizonOutcome{};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
