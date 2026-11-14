/**************************************************************************/
/* worker_rounds.cpp                                                      */
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
 * @file worker_rounds.cpp
 * @brief Implements WorkerRounds: the drain, the poll, the pick and the run of one worker round.
 * @author Mustafa Garip
 */

#include "worker_rounds.hpp"

#include <optional>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/observability/steady_time.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_submission.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/scheduler/steal_order.hpp>
#include <SushiRuntime/scheduler/dispatch_placement.hpp>
#include "dispatch_queue_selection.hpp"
#include "host_node_run.hpp"
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>
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

        WorkerRounds::WorkerRounds(const PoolSize& size, PinPolicy pinning,
                                   const Internal::Topology::TopologyView& topology,
                                   Internal::Device::DeviceQueueSet& devices, const Internal::Memory::USMAllocator& memory,
                                   PendingWork& pending, RunState& run)
            : num_workers_(size.workers), devices_(devices), memory_(memory), pending_(pending), run_(run)
        {
            for (std::size_t i = 0; i < num_workers_; ++i)
            {
                queues_[i] = make_sushi<Deque>();
                events_.push_back(std::make_unique<EventPolling::ActiveEventList>());
            }

            // Built once, here (modules/engine/scheduler/README.md).
            StealOrder order = build_steal_order(topology, num_workers_, pinning, size.first_core);
            worker_numa_ = std::move(order.worker_numa);
            victim_order_ = std::move(order.victim_order);
            local_tier_len_ = std::move(order.local_tier_len);
        }

        bool WorkerRounds::has_work(int thread_id) const
        {
            return !events_[thread_id]->is_empty() || pending_.has_any() || queues_[thread_id]->size() != 0;
        }

        bool WorkerRounds::events_empty(int thread_id) const
        {
            return events_[thread_id]->is_empty();
        }

        void WorkerRounds::add_idle_ns(int thread_id, std::uint64_t nanoseconds) noexcept
        {
            worker_time_[thread_id].idle_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
        }

        std::size_t WorkerRounds::queue_size(int worker_id) const
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            if (queues_[worker_id]) return queues_[worker_id]->size();
            return 0;
        }

        std::size_t WorkerRounds::completed_count(int worker_id) const
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            return completed_[worker_id].value.load(std::memory_order_relaxed);
        }

        void WorkerRounds::migrate(int from_worker_id, std::size_t count)
        {
            if (from_worker_id < 0 || from_worker_id >= static_cast<int>(num_workers_)) return;
            if (!queues_[from_worker_id]) return;

            std::vector<Task*> rescued_tasks;
            rescued_tasks.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                Task* task = queues_[from_worker_id]->steal();
                if (task) rescued_tasks.push_back(task);
                else break; // Stop once the queue is empty.
            }
            if (!rescued_tasks.empty())
                pending_.push_rescued(rescued_tasks);
        }

        std::vector<int> WorkerRounds::steal_victim_order(int worker_id) const
        {
            if (worker_id < 0 || static_cast<std::size_t>(worker_id) >= victim_order_.size())
                return {};
            return victim_order_[worker_id];
        }

        WorkerRounds::TimeSplit WorkerRounds::time_split(int worker_id) const noexcept
        {
            TimeSplit split;
            if (worker_id < 0 || static_cast<std::size_t>(worker_id) >= num_workers_)
                return split;
            const TimeBuckets& buckets = worker_time_[worker_id];
            split.busy_ns = buckets.busy_ns.load(std::memory_order_relaxed);
            split.stealing_ns = buckets.stealing_ns.load(std::memory_order_relaxed);
            split.polling_ns = buckets.polling_ns.load(std::memory_order_relaxed);
            split.idle_ns = buckets.idle_ns.load(std::memory_order_relaxed);
            return split;
        }

        void WorkerRounds::reset_times() noexcept
        {
            for (std::size_t i = 0; i < K_MAX_WORKER_THREADS; ++i)
            {
                worker_time_[i].busy_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].stealing_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].polling_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].idle_ns.store(0, std::memory_order_relaxed);
            }
        }

        void WorkerRounds::release_queues()
        {
            for (std::size_t i = 0; i < num_workers_; ++i)
            {
                if (queues_[i])
                {
                    while (Task* task = queues_[i]->pop())
                        task->release();
                }
            }
        }

        /**
         * @brief Drains a batch of pending submissions into a worker's local queue.
         * Prevents global queue contention by pulling tasks in batches.
         * @param thread_id The ID of the worker thread pulling the tasks.
         */
        void WorkerRounds::drain_pending_submissions(int thread_id)
        {
            if (thread_id < 0 || static_cast<std::size_t>(thread_id) >= num_workers_)
                return;

            auto& my_queue = queues_[thread_id];

            // A half-full local deque pulls nothing from the injector (modules/engine/scheduler/README.md).
            if (my_queue->size() >= Deque::K_CAPACITY / 2)
                return;

            // An empty injector skips the mutex; a stale read is safe (modules/engine/scheduler/README.md).
            if (!pending_.has_normal())
                return;

            constexpr std::size_t BATCH_SIZE = 4096;
            std::vector<Task*>& batch = drain_scratch_[thread_id];
            pending_.take_batch(batch, BATCH_SIZE);
            for (Task* task : batch)
                if (task) my_queue->push(task);
        }

        /**
         * @brief Steals a task, node-local victims first (modules/engine/scheduler/README.md).
         * @param th_id The ID of the worker attempting to steal.
         * @return Pointer to a stolen task, or nullptr if none found.
         */
        Task* WorkerRounds::find_task_to_steal(int th_id)
        {
            if (th_id < 0 || static_cast<size_t>(th_id) >= num_workers_)
                return nullptr;

            SR_ZONE_DETAIL(run_.collector, "worker.steal");

            const std::vector<int>& order = victim_order_[th_id];
            const int total = static_cast<int>(order.size());
            const int local = local_tier_len_[th_id];

            // 1. Local tier with rotating start (only meaningful when local > 0).
            if (local > 0)
            {
                static thread_local size_t local_cursor = 0;
                const int start = static_cast<int>(local_cursor++ % static_cast<size_t>(local));
                for (int k = 0; k < local; ++k)
                {
                    const int victim_id = order[(start + k) % local];
                    if (Task* stolen = queues_[victim_id]->steal())
                    {
                        local_steals_.fetch_add(1, std::memory_order_relaxed);
                        return stolen;
                    }
                }
            }

            // 2. Remote tiers, already sorted by ascending distance — sequential.
            for (int k = local; k < total; ++k)
            {
                if (Task* stolen = queues_[order[k]]->steal())
                {
                    remote_steals_.fetch_add(1, std::memory_order_relaxed);
                    return stolen;
                }
            }

            return nullptr;
        }
        /**
         * @brief Retrieves the ordered list of victim workers to steal from.
         * @param worker_id The ID of the worker.
         * @return A vector of worker IDs ordered by NUMA distance.
         */
        /**
         * @brief Runs one round of a worker's active and drain phases (modules/engine/scheduler/README.md).
         * @param thread_id The ID of the worker the round belongs to.
         */
        WorkerRounds::Round WorkerRounds::run_round(int thread_id)
        {
            auto& my_queue = queues_[thread_id];
            EventPolling::ActiveEventList& active_events = *events_[thread_id];

            const bool is_running = pending_.is_running();
            const bool is_shutdown = pending_.is_shutdown();
            const bool prof = run_.profiling.load(std::memory_order_acquire);

            // A worker parks only with an empty deque of its own (modules/engine/scheduler/README.md).
            if (!is_running && !is_shutdown && active_events.is_empty()
                && my_queue->size() == 0 && my_queue->overflow_size() == 0
                && !pending_.has_any())
                return Round::Park;

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
                    polled = run_.poller.poll_events(active_events, *my_queue);
                    add_ns(wt.polling_ns, p0, sclock::now());
                }
                else
                    polled = run_.poller.poll_events(active_events, *my_queue);
            });
            if (!poll_ran)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "poll_events threw {}: {}; the current run is aborted.",
                             to_string(poll_ran.error().code), poll_ran.error().message);
                run_.outcome.record(std::move(poll_ran.error()));
            }
            else if (polled)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "An event of the current run failed with {}: {}; the run is aborted.",
                             to_string(polled->code), polled->message);
                run_.outcome.record(std::move(*polled));
            }

            // Only a priority submission takes the global mutex (modules/engine/scheduler/README.md).
            if (pending_.has_priority())
                task = pending_.take_priority();

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
                if (is_shutdown && active_events.is_empty() && !pending_.has_any())
                    return Round::Exit;
                return Round::Idle;
            }

            if (task->is_splittable())
            {
                if (auto split_chunk = task->split())
                    my_queue->push(split_chunk.value().leak());
            }

            // An offloaded task is owned by the coordinator (modules/engine/scheduler/README.md).
            if (!run_.offload_gate.offer(*task, run_.outcome))
            {
                const auto b0 = prof ? sclock::now() : sclock::time_point{};
                execute_task(task, thread_id, active_events);
                if (prof) add_ns(wt.busy_ns, b0, sclock::now());
            }
            else
            {
                // Its quiescence count retires here (modules/engine/scheduler/README.md).
                run_.quiescence.retire();
            }
            return Round::Ran;
        }
        /** 
         * @brief Executes a single task and tracks its events.
         * @param task Pointer to the task to execute.
         * @param worker_id The executing worker's id (scheduler worker id, not the
         *        EBR slot id), used for per-worker completion accounting.
         * @param events The list of active events to track completion.
         */
        void WorkerRounds::execute_task(Task* task, int worker_id, EventPolling::ActiveEventList& events)
        {
            if (!task) return;

            SR_ZONE_FINE(run_.collector, "dispatch.task");

            // An aborting run executes no more kernels (modules/engine/scheduler/README.md).
            if (run_.outcome.is_aborting())
            {
                bool is_sentinel = task->is_graph_node() &&
                                   static_cast<SushiRuntime::Internal::Graph::Node*>(task)->is_sentinel;
                if (!is_sentinel)
                {
                    run_.skipped_aborted.fetch_add(1, std::memory_order_relaxed);
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
                    run_.skipped_disabled.fetch_add(1, std::memory_order_relaxed);
                    events.push(skipped_event(task));
                    return;
                }
            }

            // Completions are keyed by worker id, not EBR slot id (modules/engine/scheduler/README.md).
            if (worker_id >= 0 && static_cast<size_t>(worker_id) < K_MAX_WORKER_THREADS)
                completed_[worker_id].value.fetch_add(1, std::memory_order_relaxed);

            const auto abort_run = [this, task, &events](Error failure)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "{}; the run is aborted and its remaining tasks are skipped.",
                             failure.message);

                // Only this run fails; the pool stays re-armable (modules/engine/scheduler/README.md).
                run_.outcome.record(std::move(failure));

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
                const bool profiling = run_.profiling.load(std::memory_order_acquire);
                const bool host = task->execution_mode() == Task::ExecutionMode::Host;

                // A pure-CPU host task needs no queue and runs inline (see design spec §0).
                if (node && host && !node->submits_device_work())
                {
                    // The step is captured now, not read at reap, so a later round's step cannot land on this record.
                    const std::uint32_t step =
                        profiling ? run_.current_step.load(std::memory_order_acquire) : 0;
                    run_host_node(*node, profiling, step, run_.ledger, run_.collector);
                    events.push(completed_host_event(task));
                    return;
                }

                // A node is placed once, at its first dispatch (modules/engine/scheduler/README.md).
                if (node)
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
                    if (auto* ledger = run_.ledger.load(std::memory_order_acquire))
                        ledger->note_drop(DropCause::NoQueue, DropCounts::K_NO_NODE);

                // The step is read once, so the record names the step the dispatch was submitted in.
                const std::uint32_t dispatch_step = run_.current_step.load(std::memory_order_acquire);
                // The kernel cannot start before this reading, which bounds the device's clock pairing.
                const std::uint64_t submit_steady_ns = profiling ? Internal::Observability::steady_now_ns() : 0;

                // Handed-over events are collected only for the ledger (modules/engine/scheduler/README.md).
                sycl::event e;
                std::vector<sycl::event> handed_over;
                {
                    SR_ZONE_FINE(run_.collector, "dispatch.submit");
                    e = Internal::Graph::submit_task(queue, *task, node ? &node->sycl_deps : nullptr,
                                                     (profiling && host) ? &handed_over : nullptr);
                }
                const bool submitted = submitted_device_work(host, profiling, e, handed_over);

                // A submission that reached no device is pushed as ready, never dropped (modules/engine/scheduler/README.md).
                events.push(dispatched_event(e, submitted, task, node, profiling,
                                             dispatch_step,
                                             dispatch_device, std::move(handed_over), submit_steady_ns));
            });
            if (!ran)
                abort_run(name_failure(*task, ran.error()));
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
