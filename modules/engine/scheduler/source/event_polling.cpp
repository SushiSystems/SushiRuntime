/**************************************************************************/
/* event_polling.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */


/**
 * @file event_polling.cpp
 * @brief Implements the non-template members of EventPolling.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <SushiRuntime/scheduler/event_polling.hpp>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/observability/event_timestamps.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Names the coordinator told of every executed graph node this poller reaps.
         */
        void EventPolling::set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload) noexcept
        {
            offload_ = offload;
        }

        /**
         * @brief Turns per-node kernel timing capture on or off.
         */
        void EventPolling::set_profiling(bool on) noexcept
        {
            profiling_.store(on, std::memory_order_release);
        }

        /**
         * @brief Wires the plan's event ledger for the round about to run.
         */
        void EventPolling::set_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept
        {
            ledger_.store(ledger, std::memory_order_release);
        }

        /**
         * @brief Wires the scheduler's run-quiescence latch into the poller.
         */
        void EventPolling::set_inflight_tracker(QuiescenceLatch* latch) noexcept
        {
            quiescence_ = latch;
        }

        /**
         * @brief Turns an entry whose query or wait threw into what a skipped task is.
         */
        bool EventPolling::reap_failed(PolledEvent& entry)
        {
            SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "Polling the event of task '{}' threw; the task is reaped "
                         "as not executed and its successors are released.",
                         entry.task ? entry.task->name() : "<none>");
            entry.ready = true;
            entry.has_event = false;
            entry.executed = false;
            return true;
        }

        /**
         * @brief Keeps the first failure of a poll and logs a later one as dropped.
         */
        void EventPolling::keep_first_failure(std::optional<Error>& first, Error next)
        {
            if (!first)
            {
                first = std::move(next);
                return;
            }
            SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "A second event failed in the same poll: {}; the poll "
                        "returns the first failure, '{}'.", next.message, first->message);
        }

        /**
         * @brief Returns how many stalled polls waited on their oldest event since the last take.
         */
        std::uint64_t EventPolling::stall_drives() const noexcept
        {
            return stall_drives_.load(std::memory_order_relaxed);
        }

        /**
         * @brief Returns the stalled-poll count and starts it again from zero.
         */
        std::uint64_t EventPolling::take_stall_drives() noexcept
        {
            return stall_drives_.exchange(0, std::memory_order_relaxed);
        }

        /**
         * @brief Tells the installed coordinator that the pool finished a graph node.
         */
        void EventPolling::report_local_completion(Task* task) noexcept
        {
            if (task && task->is_graph_node())
                offload_->on_local_completion(*static_cast<Internal::Graph::Node*>(task));
        }

        /**
         * @brief Writes this dispatch's event record before the completion is published.
         */
        void EventPolling::accumulate_profiling(const PolledEvent& pe) noexcept
        {
            if (!pe.task)
                return;

            Internal::Observability::ProfilingLedger* ledger =
                ledger_.load(std::memory_order_acquire);
            if (ledger == nullptr)
                return;

            auto* node = pe.task->is_graph_node()
                ? static_cast<SushiRuntime::Internal::Graph::Node*>(pe.task) : nullptr;

            Internal::Observability::EventTimestamps times;
            const bool have_start_end = pe.has_event && Internal::Observability::read_dispatch_timestamps(pe.event, pe.handed_over, times);
            if (!have_start_end)
            {
                // A task that did not run, a sentinel and a native host node leave no event by design.
                if (node != nullptr && pe.executed && !node->is_sentinel)
                {
                    if (pe.has_event)
                        ledger->note_drop(DropCause::UnreadableEvent,
                                          node->registration_index);
                    else if (node->execution_mode() == SushiRuntime::Internal::Scheduler::Task::ExecutionMode::Kernel)
                        ledger->note_drop(DropCause::NoEvent,
                                          node->registration_index);
                }
                return;
            }

            if (times.has_submit && ledger->is_pairing_owner(pe.device_index, pe.task))
                ledger->set_pairing_event_ns(pe.device_index, times.submit_ns);

            if (node == nullptr)
                return;

            Internal::Observability::DispatchFacts facts;
            facts.node_index = node->registration_index;
            facts.invocation = pe.step;
            facts.launched_width = pe.widths.launched;
            facts.live_width = pe.widths.live;
            facts.device_index = pe.device_index;
            facts.submit_thread = pe.submit_thread;
            ledger->record(facts, Internal::Observability::DispatchTimes{times.submit_ns, times.start_ns, times.end_ns});
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
