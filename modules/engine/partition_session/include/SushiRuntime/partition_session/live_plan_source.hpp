/**************************************************************************/
/* live_plan_source.hpp                                                   */
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
 * @file live_plan_source.hpp
 * @brief Declares LivePlanSource, the plan source that asks the partitioners and judges each horizon.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/partition_session/partitioner_factory.hpp>
#include <SushiRuntime/partition_session/plan_source.hpp>
#include <SushiRuntime/partition_session/profile_store.hpp>
#include <SushiRuntime/partition_session/rebalance_decision.hpp>
#include <SushiRuntime/partitioning/partitioner.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Plans with the scheduler over the runtime's profiles and decides each horizon with decide_rebalance().
         *
         * A horizon folds its dispatches into the shared ProfileStore and builds the scheduler again over
         * the folded model. A floor on trial keeps the plan it replaced, which LeaveFloor puts back.
         */
        class SUSHIRUNTIME_API LivePlanSource final : public PlanSource
        {
            public:
                /**
                 * @brief Builds a source over profiles, which holds a model, with partitioners whose scheduler is set.
                 * @param margin How far a measured time may pass its model before a horizon acts.
                 */
                LivePlanSource(std::shared_ptr<ProfileStore> profiles, PartitionerFactories partitioners, double margin);

                /** @brief Partitions problem with the scheduler over the profiles in memory. */
                [[nodiscard]] Result<PartitionPlan> first(PartitionProblem problem) override;

                /** @brief Folds observed, then replans, tries the floor, keeps or leaves it, or keeps the plan. */
                [[nodiscard]] HorizonOutcome horizon(const PartitionPlan& in_force, const PartitionReport& judged,
                                                     const std::vector<DispatchObservation>& observed,
                                                     std::uint64_t horizon) override;

            private:
                /** @brief Takes the model the store holds now and builds the scheduler over it when it changed. */
                void refresh(std::shared_ptr<const CostModel> model);

                /// @brief The runtime's profiles, shared with every other session.
                std::shared_ptr<ProfileStore> profiles_;

                /// @brief Build the scheduler and the floor.
                PartitionerFactories partitioners_;

                /// @brief How far a measured time may pass its model before a horizon acts.
                double margin_;

                /// @brief The model the scheduler prices with.
                std::shared_ptr<const CostModel> model_;

                /// @brief The scheduler over model_.
                std::unique_ptr<Partitioner> scheduler_;

                /// @brief The plan a floor on trial replaced; nothing when no floor is on trial.
                std::optional<PartitionPlan> replaced_;

                /// @brief What the floor check remembers between horizons.
                RebalanceState state_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
