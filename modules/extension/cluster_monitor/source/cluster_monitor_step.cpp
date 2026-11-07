/**************************************************************************/
/* cluster_monitor_step.cpp                                               */
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
 * @file cluster_monitor_step.cpp
 * @brief Implements the pure step of one monitor pass, and the period and wake time it keeps.
 * @author Mustafa Garip
 * @see modules/extension/cluster_monitor/README.md
 */

#include <SushiRuntime/cluster_monitor/cluster_monitor_step.hpp>

#include <algorithm>
#include <cstddef>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the passes that fit into the tightest timeout the monitor applies. */
            constexpr std::int64_t K_PASSES_PER_TIMEOUT = 4;

            /** @brief Holds the shortest time between two passes, in milliseconds. */
            constexpr std::int64_t K_MINIMUM_PERIOD_MS = 1;

            /** @brief Holds the time in which one worker gets at most one reap warning. */
            constexpr std::chrono::seconds K_REAP_WARNING_INTERVAL{1};

            /** @brief Holds the words a log line uses for each ReapCause, by its value. */
            constexpr const char* K_REAP_CAUSE_WORDS[] = {
                "no result within the task deadline",
                "the worker did not acknowledge the dispatch",
                "the worker's heartbeats stopped listing it",
            };
        } // namespace

        MonitorActions plan_pass(MonitorState& state, const MonitorFindings& findings,
                                 std::chrono::steady_clock::time_point now)
        {
            MonitorActions actions;
            actions.goodbye_and_close = findings.silent;
            actions.close = findings.handshake_overdue;

            for (const ReapedTask& task : findings.reaped)
            {
                switch (task.cause)
                {
                    case ReapCause::Deadline:
                        ++actions.reaped.deadline;
                        break;
                    case ReapCause::Unacknowledged:
                        ++actions.reaped.unacknowledged;
                        break;
                    case ReapCause::LostResult:
                        ++actions.reaped.lost_result;
                        break;
                }
            }
            actions.reoffer_held = !findings.reaped.empty();

            for (auto entry = state.last_reap_warning.begin();
                 entry != state.last_reap_warning.end();)
            {
                if (now - entry->second >= K_REAP_WARNING_INTERVAL)
                    entry = state.last_reap_warning.erase(entry);
                else
                    ++entry;
            }

            for (const ReapedTask& task : findings.reaped)
            {
                if (state.last_reap_warning.try_emplace(task.worker.value, now).second)
                    actions.warnings.push_back(ReapWarning{task.task_id, task.worker, task.cause});
            }
            return actions;
        }

        std::chrono::milliseconds pass_period(std::chrono::milliseconds configured,
                                              std::chrono::milliseconds liveness_timeout,
                                              std::chrono::milliseconds task_deadline) noexcept
        {
            std::int64_t tightest = liveness_timeout.count();
            if (task_deadline.count() > 0 && task_deadline.count() < tightest)
                tightest = task_deadline.count();

            const std::int64_t wanted =
                std::min<std::int64_t>(configured.count(), tightest / K_PASSES_PER_TIMEOUT);
            return std::chrono::milliseconds(std::max(wanted, K_MINIMUM_PERIOD_MS));
        }

        std::chrono::steady_clock::time_point next_pass_at(
            std::chrono::steady_clock::time_point now, std::chrono::milliseconds period,
            std::chrono::steady_clock::time_point earliest_hold,
            std::chrono::steady_clock::time_point earliest_deadline) noexcept
        {
            return std::min({now + period, earliest_hold, earliest_deadline});
        }

        const char* reap_cause_words(ReapCause cause) noexcept
        {
            return K_REAP_CAUSE_WORDS[static_cast<std::size_t>(cause)];
        }

    } // namespace Distributed
} // namespace SushiRuntime
