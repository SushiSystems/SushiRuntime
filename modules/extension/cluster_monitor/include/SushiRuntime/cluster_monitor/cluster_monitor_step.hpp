/**************************************************************************/
/* cluster_monitor_step.hpp                                               */
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
 * @file cluster_monitor_step.hpp
 * @brief Declares the pure step of one monitor pass, and the period and wake time it keeps.
 * @author Mustafa Garip
 * @see modules/extension/cluster_monitor/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/pending_tasks/pending_tasks.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds one task a pass took back, without the node it owned.
         */
        struct ReapedTask
        {
            /** @brief Holds the master's id for the task. */
            std::uint64_t task_id = 0;

            /** @brief Holds the worker the task was out on. */
            PeerID worker;

            /** @brief Holds why the table removed it. */
            ReapCause cause = ReapCause::Deadline;
        };

        /**
         * @brief Holds what the directory, the table and the holds reported to one pass.
         */
        struct MonitorFindings
        {
            /** @brief Holds the workers that sent no frame within the liveness timeout. */
            std::vector<PeerID> silent;

            /** @brief Holds the peers that did not finish the handshake in time. */
            std::vector<PeerID> handshake_overdue;

            /** @brief Holds the tasks the table took back. */
            std::vector<ReapedTask> reaped;
        };

        /**
         * @brief Holds the counts of the tasks a pass took back, by cause.
         */
        struct ReapCounts
        {
            std::uint64_t deadline = 0;       /**< Tasks past the task deadline. */
            std::uint64_t unacknowledged = 0; /**< Dispatches nobody confirmed. */
            std::uint64_t lost_result = 0;    /**< Tasks a worker stopped holding. */
        };

        /**
         * @brief Holds one reaped task the pass logs.
         */
        struct ReapWarning
        {
            /** @brief Holds the master's id for the task. */
            std::uint64_t task_id = 0;

            /** @brief Holds the worker the task was taken back from. */
            PeerID worker;

            /** @brief Holds why the task was taken back. */
            ReapCause cause = ReapCause::Deadline;
        };

        /**
         * @brief Holds what a pass does with its findings, in the order the shell applies it.
         */
        struct MonitorActions
        {
            /** @brief Holds the silent workers: send GOODBYE, then close the peer. */
            std::vector<PeerID> goodbye_and_close;

            /** @brief Holds the peers to close without a GOODBYE. */
            std::vector<PeerID> close;

            /** @brief Holds the reaped tasks, counted by cause. */
            ReapCounts reaped;

            /** @brief Holds the reaped tasks to log: at most one per worker per second. */
            std::vector<ReapWarning> warnings;

            /** @brief Tells whether the nodes held for a busy worker are offered again. */
            bool reoffer_held = false;
        };

        /**
         * @brief Holds what the step remembers between two passes.
         */
        struct MonitorState
        {
            /** @brief Holds the time of the last reap warning per worker id value. */
            std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point>
                last_reap_warning;
        };

        /**
         * @brief Decides what one pass does with what it found.
         *
         * Silent workers get a GOODBYE and a close, late handshakes a close; reaped tasks are
         * counted by cause, and a reap offers the held nodes again. @p state drops warning
         * times older than a second.
         */
        [[nodiscard]] SUSHIRUNTIME_API MonitorActions plan_pass(
            MonitorState& state, const MonitorFindings& findings,
            std::chrono::steady_clock::time_point now);

        /**
         * @brief Returns the time between two passes.
         *
         * The configured period, or a quarter of the tightest timeout when shorter; at least 1 ms.
         * @param configured The period the options ask for.
         * @param liveness_timeout The silence after which a worker is closed.
         * @param task_deadline The bound on a kernel's run; 0 or less sets no bound.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::chrono::milliseconds pass_period(
            std::chrono::milliseconds configured, std::chrono::milliseconds liveness_timeout,
            std::chrono::milliseconds task_deadline) noexcept;

        /**
         * @brief Returns when the next pass runs: the earliest of a period away, the earliest
         *        held wait and the earliest task deadline.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::chrono::steady_clock::time_point next_pass_at(
            std::chrono::steady_clock::time_point now, std::chrono::milliseconds period,
            std::chrono::steady_clock::time_point earliest_hold,
            std::chrono::steady_clock::time_point earliest_deadline) noexcept;

        /** @brief Returns the words a log line uses for a ReapCause. */
        [[nodiscard]] SUSHIRUNTIME_API const char* reap_cause_words(ReapCause cause) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
