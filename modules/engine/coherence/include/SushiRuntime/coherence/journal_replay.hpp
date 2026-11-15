/**************************************************************************/
/* journal_replay.hpp                                                     */
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
 * @file journal_replay.hpp
 * @brief Declares replay_placement(), which answers a problem again from a placement journal entry.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/coherence/partition_assignment.hpp>
#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/coherence/partitioner_factory.hpp>
#include <SushiRuntime/coherence/placement_journal.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns the answer the partitioner entry names gives problem when priced by entry's profiles.
         *
         * A partitioner is a pure function of the problem and the profiles, so the answer equals the one
         * the entry records.
         * @return InvalidArgument when problem does not hash to the entry's problem, InvalidState when the
         *         answer does not hash to its assignment, or the error of a refusal.
         */
        [[nodiscard]] Result<PartitionAssignment> replay_placement(const PlacementJournalEntry& entry,
                                                                   const PartitionProblem& problem,
                                                                   const PartitionerFactories& factories);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
