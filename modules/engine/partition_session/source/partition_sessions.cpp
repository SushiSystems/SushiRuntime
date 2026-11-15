/**************************************************************************/
/* partition_sessions.cpp                                                 */
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
 * @file partition_sessions.cpp
 * @brief Implements PartitionSessions.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/partition_sessions.hpp>

#include <cstdio>
#include <string>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/partition_session/journal_plan_source.hpp>
#include <SushiRuntime/partition_session/live_plan_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PartitionSessions::PartitionSessions(PartitionSessionsParts parts) : parts_(std::move(parts))
        {
            SR_VERIFY(parts_.profiles != nullptr, "PartitionSessions: pass the runtime's profile store, not null.");
            SR_VERIFY(parts_.copies != nullptr, "PartitionSessions: pass the tally of the runtime's copies, not null.");
        }

        bool PartitionSessions::is_active() const
        {
            if (parts_.partitioners.scheduler && parts_.profiles->model() != nullptr)
                return true;
            const std::lock_guard<std::mutex> lock(mutex_);
            return !replays_.empty();
        }

        Result<void> PartitionSessions::replay(std::vector<PlacementJournalEntry> entries)
        {
            if (entries.empty() || entries.front().reason != PlacementReason::First)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "PartitionSessions::replay(): a placement journal opens with a first "
                                        "partition"});
            const std::uint64_t hash = entries.front().problem_hash;
            const std::lock_guard<std::mutex> lock(mutex_);
            if (opened_.count(hash) != 0)
                return make_error(Error{ErrorCode::InvalidState,
                                        "PartitionSessions::replay(): a graph with this first problem already "
                                        "partitioned; replay before its first partition"});
            replays_[hash].push_back(std::move(entries));
            return {};
        }

        std::shared_ptr<PartitionSession> PartitionSessions::open(std::uint64_t first_problem_hash)
        {
            std::unique_ptr<PlanSource> source;
            std::uint64_t occurrence = 0;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const auto waiting = replays_.find(first_problem_hash);
                if (waiting != replays_.end())
                {
                    source = std::make_unique<JournalPlanSource>(std::move(waiting->second.front()),
                                                                 parts_.partitioners);
                    waiting->second.pop_front();
                    if (waiting->second.empty())
                        replays_.erase(waiting);
                }
                else if (parts_.partitioners.scheduler && parts_.profiles->model() != nullptr)
                    source = std::make_unique<LivePlanSource>(parts_.profiles, parts_.partitioners, parts_.margin);
                else
                    return nullptr;
                occurrence = opened_[first_problem_hash]++;
            }
            PartitionSessionParts parts;
            parts.devices = parts_.devices;
            parts.copies = parts_.copies;
            parts.device_of = parts_.device_of;
            if (parts_.journal_base)
                parts.journal_file = journal_path(*parts_.journal_base, first_problem_hash, occurrence);
            return std::make_shared<PartitionSession>(std::move(parts), std::move(source));
        }

        void PartitionSessions::partitioned(const std::shared_ptr<const PartitionSession>& session)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            last_ = session;
            partitioned_.store(true, std::memory_order_release);
        }

        PartitionReport PartitionSessions::last_report() const
        {
            std::shared_ptr<const PartitionSession> last;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                last = last_.lock();
            }
            return last != nullptr ? last->report() : PartitionReport{};
        }

        std::filesystem::path PartitionSessions::journal_path(const std::filesystem::path& base,
                                                              std::uint64_t first_problem_hash,
                                                              std::uint64_t occurrence)
        {
            char digits[17] = {};
            std::snprintf(digits, sizeof(digits), "%016llx", static_cast<unsigned long long>(first_problem_hash));
            std::string name = base.filename().string() + "." + digits;
            if (occurrence != 0)
                name += "-" + std::to_string(occurrence);
            name += ".journal";
            return base.parent_path() / name;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
