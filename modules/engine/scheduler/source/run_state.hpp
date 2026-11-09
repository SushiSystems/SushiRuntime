/**************************************************************************/
/* run_state.hpp                                                          */
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
 * @file run_state.hpp
 * @brief Declares RunState, the state of the active run that the rounds and the scheduler share.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/offload_gate.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Holds what the active run keeps: its outcome, its event poller and latch, its offload gate,
         *        its profiling switches and its skip counts.
         * @see modules/engine/scheduler/README.md
         */
        struct RunState
        {
            /** @brief Builds the state of a scheduler whose zones record into @p collector. */
            explicit RunState(Profile::Collector& collector) noexcept : collector(collector) {}

            /** @brief Logs and clears what the finished run left unrun and how often a poll waited on its oldest event. */
            void log_run_tail() noexcept;

            RunOutcome outcome;                  /**< Failure and cancellation of the active run. */
            EventPolling poller;                 /**< Tracks the SYCL events in flight. */
            QuiescenceLatch quiescence;          /**< Counts tasks submitted for the run but not yet reaped. */
            OffloadGate offload_gate;            /**< Offers each offloadable node to the installed coordinator. */
            std::atomic<bool> profiling{false};  /**< Whether the run accumulates per-worker time buckets. */
            std::atomic<Internal::Observability::ProfilingLedger*> ledger{nullptr}; /**< The armed round's ledger, or null. */
            std::atomic<std::uint32_t> next_step{0};     /**< Counter arm_for_execution() draws the next round's step from. */
            std::atomic<std::uint32_t> current_step{0};  /**< This round's step, captured into each dispatched event. */
            std::atomic<std::uint64_t> skipped_aborted{0}; /**< Tasks skipped because the run was aborting, since the last take. */
            std::atomic<std::uint64_t> skipped_disabled{0}; /**< Tasks skipped because their control was off, since the last take. */
            Profile::Collector& collector;       /**< Takes the scheduler's dispatch and steal zones. */
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
