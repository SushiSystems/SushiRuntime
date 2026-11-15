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

#include <SushiRuntime/coherence/partition_report_builder.hpp>
#include <SushiRuntime/core/assert.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PartitionSession::PartitionSession(PartitionSessionParts parts) : parts_(std::move(parts))
        {
            SR_VERIFY(parts_.copies != nullptr, "PartitionSession: pass the tally of the runtime's copies, not null.");
        }

        const Result<PartitionAssignment, PartitionRefusal>& PartitionSession::partition(PartitionProblem problem)
        {
            SR_VERIFY(is_active(), "PartitionSession::partition(): the session has no partitioner; check is_active().");
            Result<PartitionAssignment, PartitionRefusal> answer = parts_.partitioner->partition(problem);
            last_.reset();
            last_.emplace(Plan{std::move(problem), std::move(answer)});
            ledger_.clear();
            parts_.copies->clear();
            return last_->answer;
        }

        PartitionReport PartitionSession::report() const
        {
            if (!last_ || parts_.model == nullptr)
                return PartitionReport{};
            return make_partition_report(last_->problem, *parts_.model, last_->answer,
                                         ledger_.measurement(parts_.copies->tallies(), parts_.device_of));
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
