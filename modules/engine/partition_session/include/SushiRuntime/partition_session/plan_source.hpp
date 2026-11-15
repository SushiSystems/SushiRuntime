/**************************************************************************/
/* plan_source.hpp                                                        */
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
 * @file plan_source.hpp
 * @brief Declares PlanSource, the port a PartitionSession takes its plans from, with PartitionPlan and HorizonOutcome.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>
#include <SushiRuntime/partition_session/placement_journal.hpp>
#include <SushiRuntime/partition_session/rebalance_decision.hpp>
#include <SushiRuntime/partitioning/partition_assignment.hpp>
#include <SushiRuntime/partitioning/partition_problem.hpp>
#include <SushiRuntime/partitioning/partition_refusal.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds a problem, the answer to it and the model that priced the answer. */
        struct PartitionPlan
        {
            PartitionProblem problem;                               /**< The problem. */
            std::uint64_t problem_hash = 0;                         /**< problem_hash() of problem. */
            Result<PartitionAssignment, PartitionRefusal> answer;   /**< The answer; a refusal is a plan too. */
            std::shared_ptr<const CostModel> model;                 /**< The model the answer was priced with. */
            PlannedBy planned_by = PlannedBy::Scheduler;            /**< The partitioner that answered. */
        };

        /** @brief Holds what a horizon does: the action and, when the plan changes, the plan to put in force. */
        struct HorizonOutcome
        {
            RebalanceAction action = RebalanceAction::Keep; /**< What the horizon did; Keep journals nothing. */
            std::optional<PartitionPlan> plan;              /**< The plan to put in force; nothing keeps the plan in force. */
        };

        /**
         * @brief Gives a PartitionSession its plans: the first plan of a problem and what each horizon does.
         *
         * LivePlanSource asks the partitioners; JournalPlanSource answers from a placement journal. A
         * source belongs to one session and is called between that session's runs only.
         */
        class PlanSource
        {
            public:
                virtual ~PlanSource() = default;

                /**
                 * @brief Returns the plan to put in force for problem.
                 * @return The plan, whose answer may be a refusal, or the error that leaves nothing in force.
                 */
                [[nodiscard]] virtual Result<PartitionPlan> first(PartitionProblem problem) = 0;

                /**
                 * @brief Returns what a horizon does with the plan in force.
                 * @param in_force The plan in force; its answer is not a refusal.
                 * @param judged   The report of in_force with what its runs measured.
                 * @param observed The dispatches the runs of the horizon measured.
                 * @param horizon  The horizons the session has judged, this one included.
                 */
                [[nodiscard]] virtual HorizonOutcome horizon(const PartitionPlan& in_force, const PartitionReport& judged,
                                                             const std::vector<DispatchObservation>& observed,
                                                             std::uint64_t horizon) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
