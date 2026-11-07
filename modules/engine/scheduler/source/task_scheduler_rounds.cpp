/**************************************************************************/
/* task_scheduler_rounds.cpp                                             */
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
 * @file task_scheduler_rounds.cpp
 * @brief Runs the dispatch rounds of the worker pool: draining submissions, one round, one task.
 * @author Mustafa Garip
 */

#include <new>
#include <thread>
#include <optional>
#include <mutex>
#include <deque>
#include <atomic>
#include <random>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/observability/pairing_claim.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_submission.hpp>
#include <SushiRuntime/topology/affinity.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/scheduler/steal_order.hpp>
#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>
#include "dispatch_placement.hpp"
#include "dispatch_queue_selection.hpp"
#include "host_node_run.hpp"
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_state.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        using namespace SushiRuntime;

        namespace
        {
            /** @brief NUMA node whose devices serve a task that is not a graph node. */
            constexpr int K_TASK_NUMA_NODE = 0;

            /**
             * @brief Builds the ready event of a task whose work did not run to its end.
             * @return An event the poller reaps at once and reports to no offload coordinator.
             */
            EventPolling::PolledEvent skipped_event(Task* task)
            {
                EventPolling::PolledEvent skipped;
                skipped.task = task;
                skipped.ready = true;
                skipped.executed = false;
                return skipped;
            }
        } // namespace
        /**
         * @brief Checks if there are any pending submissions in the global queue.
         * @return true if there are pending tasks, false otherwise.
         */
        bool TaskScheduler::has_pending_submissions()
        {
            // The shadow counters are read without the lock (modules/engine/scheduler/README.md).
            return pending_count_.load(std::memory_order_relaxed) != 0
                || priority_count_.load(std::memory_order_relaxed) != 0;
        }
        /**
         * @brief Drains a batch of pending submissions into a worker's local queue.
         * Prevents global queue contention by pulling tasks in batches.
         * @param thread_id The ID of the worker thread pulling the tasks.
         */
        void TaskScheduler::drain_pending_submissions(int thread_id)
        {
            if (thread_id < 0 || static_cast<size_t>(thread_id) >= num_workers_)
                return;

            auto& my_queue = worker_queues[thread_id];
            
            // A half-full local deque pulls nothing from the injector (modules/engine/scheduler/README.md).
            if (my_queue->size() >= Deque::K_CAPACITY / 2)
                return;

            // An empty injector skips the mutex; a stale read is safe (modules/engine/scheduler/README.md).
            if (pending_count_.load(std::memory_order_relaxed) == 0)
                return;

            // The batch buffer is per worker and keeps its capacity (modules/engine/scheduler/README.md).
            constexpr size_t BATCH_SIZE = 4096;
            std::vector<Task*>& batch = drain_scratch_[thread_id];
            batch.clear();

            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                if (pending_submissions.empty()) return;

                size_t grab_count = std::min(BATCH_SIZE, pending_submissions.size());
                batch.reserve(grab_count);

                for (size_t i = 0; i < grab_count; ++i)
                {
                    batch.push_back(pending_submissions.front());
                    pending_submissions.pop_front();
                }
                pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);

                // If there's still work left in the global injector, wake up another worker
                if (!pending_submissions.empty())
                    pending_cv.notify_one();
            }

            for (Task* task : batch)
                if (task) my_queue->push(task);
        }
        /**
         * @brief Runs one round of a worker's active and drain phases (modules/engine/scheduler/README.md).
         * @param thread_id The ID of the worker the round belongs to.
         */
        TaskScheduler::WorkerRound TaskScheduler::run_round(int thread_id)
        {
            auto& my_queue = worker_queues[thread_id];
            EventPolling::ActiveEventList& active_events = *worker_events_[thread_id];

            const bool is_running = running.load(std::memory_order_acquire);
            const bool is_shutdown = shutdown_requested.load(std::memory_order_acquire);
            const bool prof = profiling_active_.load(std::memory_order_acquire);

            // A worker parks only with an empty deque of its own (modules/engine/scheduler/README.md).
            if (!is_running && !is_shutdown && active_events.is_empty()
                && my_queue->size() == 0 && my_queue->overflow_size() == 0
                && !has_pending_submissions())
                return WorkerRound::Park;

            // Clock reads are gated on profiling_active_ (modules/engine/scheduler/README.md).
            using sclock = std::chrono::steady_clock;
            auto& wt = worker_time_[thread_id];
            auto add_ns = [](std::atomic<std::uint64_t>& bucket,
                             sclock::time_point a, sclock::time_point b) noexcept
            {
                bucket.fetch_add(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()),
                    std::memory_order_relaxed);
            };

            drain_pending_submissions(thread_id);
            Task* task = nullptr;

            // A failed poll fails the run, not the thread (scheduler/README.md).
            std::optional<Error> polled;
            Result<void> poll_ran = SYCL::translate_exceptions([&]()
            {
                if (prof)
                {
                    const auto p0 = sclock::now();
                    polled = event_poller.poll_events(active_events, *my_queue);
                    add_ns(wt.polling_ns, p0, sclock::now());
                }
                else
                    polled = event_poller.poll_events(active_events, *my_queue);
            });
            if (!poll_ran)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "poll_events threw {}: {}; the current run is aborted.",
                             to_string(poll_ran.error().code), poll_ran.error().message);
                outcome_.record(std::move(poll_ran.error()));
            }
            else if (polled)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "An event of the current run failed with {}: {}; the run is aborted.",
                             to_string(polled->code), polled->message);
                outcome_.record(std::move(*polled));
            }

            // Only a priority submission takes the global mutex (modules/engine/scheduler/README.md).
            if (priority_count_.load(std::memory_order_relaxed) > 0)
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                if (!priority_submissions.empty())
                {
                    task = priority_submissions.top();
                    priority_submissions.pop();
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                }
            }

            if (!task)
                task = my_queue->pop();

            if (!task)
            {
                if (prof)
                {
                    const auto s0 = sclock::now();
                    task = find_task_to_steal(thread_id);
                    add_ns(wt.stealing_ns, s0, sclock::now());
                }
                else
                    task = find_task_to_steal(thread_id);
            }

            if (!task)
            {
                if (is_shutdown && active_events.is_empty() && !has_pending_submissions())
                    return WorkerRound::Exit;
                return WorkerRound::Idle;
            }

            if (task->is_splittable())
            {
                if (auto split_chunk = task->split())
                    my_queue->push(split_chunk.value().leak());
            }

            // An offloaded task is owned by the coordinator (modules/engine/scheduler/README.md).
            if (!offload_gate_.offer(*task, outcome_))
            {
                const auto b0 = prof ? sclock::now() : sclock::time_point{};
                execute_task(task, thread_id, active_events);
                if (prof) add_ns(wt.busy_ns, b0, sclock::now());
            }
            else
            {
                // Its quiescence count retires here (modules/engine/scheduler/README.md).
                quiescence_.retire();
            }
            return WorkerRound::Ran;
        }
        /** 
         * @brief Executes a single task and tracks its events.
         * @param task Pointer to the task to execute.
         * @param worker_id The executing worker's id (scheduler worker id, not the
         *        EBR slot id), used for per-worker completion accounting.
         * @param events The list of active events to track completion.
         */
        void TaskScheduler::execute_task(Task* task, int worker_id, EventPolling::ActiveEventList& events)
        {
            if (!task) return;

            SR_ZONE_FINE("dispatch.task");

            // An aborting run executes no more kernels (modules/engine/scheduler/README.md).
            if (outcome_.is_aborting())
            {
                bool is_sentinel = task->is_graph_node() &&
                                   static_cast<SushiRuntime::Internal::Graph::Node*>(task)->is_sentinel;
                if (!is_sentinel)
                {
                    skipped_aborted_.fetch_add(1, std::memory_order_relaxed);
                    events.push(skipped_event(task));
                    return;
                }
            }

            // A disabled late-bound node runs no kernel (modules/engine/scheduler/README.md).
            if (task->is_graph_node())
            {
                auto* gnode = static_cast<SushiRuntime::Internal::Graph::Node*>(task);
                if (gnode->bindings_ &&
                    !gnode->bindings_->enabled.load(std::memory_order_relaxed))
                {
                    skipped_disabled_.fetch_add(1, std::memory_order_relaxed);
                    events.push(skipped_event(task));
                    return;
                }
            }

            // Completions are keyed by worker id, not EBR slot id (modules/engine/scheduler/README.md).
            if (worker_id >= 0 && static_cast<size_t>(worker_id) < K_MAX_WORKER_THREADS)
                worker_completed_tasks[worker_id].value.fetch_add(1, std::memory_order_relaxed);

            const auto abort_run = [this, task, &events](Error failure)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "{}; the run is aborted and its remaining tasks are skipped.",
                             failure.message);

                // Only this run fails; the pool stays re-armable (modules/engine/scheduler/README.md).
                outcome_.record(std::move(failure));

                // The failed task is pushed as ready so successors fire (modules/engine/scheduler/README.md).
                events.push(skipped_event(task));
            };

            Result<void> ran = SYCL::translate_exceptions([&]()
            {
                SR_LOG_TRACE(K_SCHEDULER_LOG_CATEGORY, "Executing task: {}", task->name());
                
                SushiRuntime::Internal::Graph::Node* node = nullptr;

                // The downcast uses the is_graph_node() tag, not RTTI (modules/engine/scheduler/README.md).
                if (task->is_graph_node())
                    node = static_cast<SushiRuntime::Internal::Graph::Node*>(task);

                // Timing follows this run's plan flag, not the context-wide setting; read once per dispatch.
                const bool profiling = profiling_active_.load(std::memory_order_acquire);
                const bool host = task->execution_mode() == Task::ExecutionMode::Host;

                // A pure-CPU host task needs no queue and runs inline (see design spec §0).
                if (node && host && !node->submits_device_work())
                {
                    // The step is captured now, not read at reap, so a later round's step cannot land on this record.
                    const std::uint32_t step =
                        profiling ? current_dispatch_step_.load(std::memory_order_acquire) : 0;
                    run_host_node(*node, profiling, step, active_ledger_);
                    events.push(completed_host_event(task));
                    return;
                }

                // A node is placed once, at its first dispatch (modules/engine/scheduler/README.md).
                if (node && !node->locality_checked_)
                {
                    const Result<void> placed =
                        place_for_dispatch(*node, memory_, devices_.count());
                    if (!placed)
                    {
                        abort_run(name_failure(*task, placed.error()));
                        return;
                    }
                }

                const DispatchQueueSelection selection =
                    select_dispatch_queue(devices_, node, profiling, K_TASK_NUMA_NODE);
                const std::uint32_t dispatch_device = selection.device_index;
                sycl::queue& queue = *selection.queue;

                // The queue is not in the device set, so the dispatch has no profiling queue to time on.
                if (selection.unmatched)
                    if (auto* ledger = active_ledger_.load(std::memory_order_acquire))
                        ledger->note_drop(DropCause::NoQueue, DropCounts::K_NO_NODE);

                Internal::Observability::PairingClaim claim(
                    claims_pairing(profiling, host, node) ? active_ledger_.load(std::memory_order_acquire)
                                                           : nullptr,
                    dispatch_device, task);

                // Handed-over events are collected only for the ledger (modules/engine/scheduler/README.md).
                sycl::event e;
                std::vector<sycl::event> handed_over;
                {
                    SR_ZONE_FINE("dispatch.submit");
                    e = Internal::Graph::submit_task(queue, *task, node ? &node->sycl_deps : nullptr,
                                                     (profiling && host) ? &handed_over : nullptr);
                }
                const bool submitted = submitted_device_work(host, profiling, e, handed_over);
                claim.settle(submitted);

                // A submission that reached no device is pushed as ready, never dropped (modules/engine/scheduler/README.md).
                events.push(dispatched_event(e, submitted, task, node, profiling,
                                             current_dispatch_step_.load(std::memory_order_acquire),
                                             dispatch_device, std::move(handed_over)));
            });
            if (!ran)
                abort_run(name_failure(*task, ran.error()));
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
