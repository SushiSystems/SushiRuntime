/**************************************************************************/
/* journal_plan_source.hpp                                                */
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
 * @file journal_plan_source.hpp
 * @brief Declares JournalPlanSource, the plan source that answers from the entries of a placement journal.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/partition_session/partitioner_factory.hpp>
#include <SushiRuntime/partition_session/placement_journal.hpp>
#include <SushiRuntime/partition_session/plan_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Puts in force the plans a journal records, at the horizons they were made at, and judges nothing.
         *
         * Each first() takes the next entry, which must be a First entry for the same problem. A horizon
         * takes the next entry when it is not a First entry and was made at this horizon, or carries no
         * horizon; any other horizon keeps the plan. What the runs measured folds into nothing.
         */
        class SUSHIRUNTIME_API JournalPlanSource final : public PlanSource
        {
            public:
                /** @brief Builds a source over entries, replayed with partitioners. */
                JournalPlanSource(std::vector<PlacementJournalEntry> entries, PartitionerFactories partitioners);

                /** @brief Answers problem from the next entry; an entry of another reason or problem is the error. */
                [[nodiscard]] Result<PartitionPlan> first(PartitionProblem problem) override;

                /** @brief Puts the next entry in force when it was made at horizon; a failed replay keeps the plan. */
                [[nodiscard]] HorizonOutcome horizon(const PartitionPlan& in_force, const PartitionReport& judged,
                                                     const std::vector<DispatchObservation>& observed,
                                                     std::uint64_t horizon) override;

            private:
                /** @brief Returns the plan entry gives problem, priced by the entry's profiles. */
                [[nodiscard]] Result<PartitionPlan> replay(const PlacementJournalEntry& entry, PartitionProblem problem) const;

                /// @brief The entries answered from, in order.
                std::vector<PlacementJournalEntry> entries_;

                /// @brief Build the partitioner an entry names.
                PartitionerFactories partitioners_;

                /// @brief The next entry of entries_ to answer from.
                std::size_t next_ = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
