/**************************************************************************/
/* journal_replay.cpp                                                     */
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
 * @file journal_replay.cpp
 * @brief Implements replay_placement().
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/journal_replay.hpp>

#include <memory>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/partitioning/assignment_hash.hpp>
#include <SushiRuntime/partitioning/problem_hash.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        Result<PartitionAssignment> replay_placement(const PlacementJournalEntry& entry, const PartitionProblem& problem,
                                                     const PartitionerFactories& factories)
        {
            if (problem_hash(problem) != entry.problem_hash)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "replay_placement(): the problem is not the one entry " +
                                            std::to_string(entry.sequence) + " answered"});
            const PartitionerFactory& factory = factories.of(entry.planned_by);
            if (!factory)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "replay_placement(): no factory builds the partitioner the entry names"});
            const std::unique_ptr<Partitioner> partitioner =
                factory(std::make_shared<const CostModel>(entry.profiles));
            Result<PartitionAssignment, PartitionRefusal> answer = partitioner->partition(problem);
            if (!answer)
                return make_error(answer.error().to_error());
            if (assignment_hash(answer.value()) != entry.assignment_hash)
                return make_error(Error{ErrorCode::InvalidState,
                                        "replay_placement(): entry " + std::to_string(entry.sequence) +
                                            " replays to another assignment"});
            return std::move(answer.value());
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
