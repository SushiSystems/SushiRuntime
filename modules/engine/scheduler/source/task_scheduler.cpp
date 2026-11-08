/**************************************************************************/
/* task_scheduler.cpp                                                     */
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
 * @file task_scheduler.cpp
 * @brief Runs the worker pool that dispatches, steals and completes graph tasks.
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

        /**
         * @brief Sizes the worker pool from the requested settings (modules/engine/scheduler/README.md).
         */
        TaskScheduler::TaskScheduler(const WorkerPoolSettings& pool,
                                     const Internal::Topology::TopologyView& topology,
                                     Internal::Device::DeviceQueueSet& devices,
                                     const Internal::Memory::USMAllocator& memory)
            : topology_(topology), devices_(devices), memory_(memory)
        {
            const PoolSize size = size_worker_pool(pool, std::thread::hardware_concurrency());
            num_workers_ = size.workers;
            pinning_ = pool.pinning;
            first_core_ = size.first_core;

            round_posted_ = std::make_unique<std::atomic<bool>[]>(num_workers_);
            for(size_t i=0; i<num_workers_; ++i)
            {
                worker_queues[i] = make_sushi<Deque>();
                worker_events_.push_back(std::make_unique<EventPolling::ActiveEventList>());
            }

            build_steal_topology();

            // The poller owns the run-quiescence counter's updates (modules/engine/scheduler/README.md).
            event_poller.set_inflight_tracker(&quiescence_);

            if (pinning_ == PinPolicy::Pin)
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} workers pinned to cores [{}, {}).",
                            num_workers_, first_core_, first_core_ + num_workers_);
            }
            else
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} unpinned workers.", num_workers_);
            }
        }
        /**
         * @brief Wires the plan's event ledger for the round about to be armed.
         */
        void TaskScheduler::set_profiling_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept
        {
            active_ledger_.store(ledger, std::memory_order_release);
            event_poller.set_ledger(ledger);
        }
        /**
         * @brief Arms the scheduler for a new execution round.
         * Signals worker threads to wake up and begin processing tasks.
         */
        void TaskScheduler::arm_for_execution(bool profiling)
        {
            // Wakes workers for a new round; clears the previous round's failure.
            outcome_.clear_failure();

            // The poller gets the profiling flag before any worker runs (modules/engine/scheduler/README.md).
            event_poller.set_profiling(profiling);
            profiling_active_.store(profiling, std::memory_order_release);

            // The flag is one term of the idle wait predicate, so it is set under pending_mutex.
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                running.store(true, std::memory_order_release);
            }
            pending_cv.notify_all();
            post_rounds();
        }
        /**
         * @brief Returns the pool to its parked state after a run quiesces.
         *
         * Under pending_mutex for the same reason arm_for_execution() is: the flag
         * is one term of the idle wait predicate. No notify — nothing waits for the
         * pool to become idle, and a worker cannot already be parked while the flag
         * is set. See the header for why this may only be called after quiescence.
         */
        void TaskScheduler::disarm() noexcept
        {
            // The ledger pointer must not dangle past its owning CompiledGraph's lifetime.
            set_profiling_ledger(nullptr);
            log_run_tail();
            SR_LOCK_WAIT("lock.pending", pending_mutex);
            running.store(false, std::memory_order_release);
        }
        /**
         * @brief Logs the tasks the run skipped and the polls that waited on their oldest event.
         */
        void TaskScheduler::log_run_tail() noexcept
        {
            const std::uint64_t aborted = skipped_aborted_.exchange(0, std::memory_order_relaxed);
            const std::uint64_t disabled = skipped_disabled_.exchange(0, std::memory_order_relaxed);
            const std::uint64_t stall_drives = event_poller.take_stall_drives();

            if (aborted != 0 && outcome_.is_cancelled())
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "The run was cancelled; {} tasks were skipped without running.",
                            aborted);
            else if (aborted != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "The run failed; {} tasks were skipped without running.",
                             aborted);
            if (disabled != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} tasks were skipped because their control was off.",
                             disabled);
            if (stall_drives != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} polls waited on their oldest event after stalling.",
                             stall_drives);
        }
        /**
         * @brief Destroys the TaskScheduler.
         * Ensures that the scheduler is stopped before destruction.
         */
        TaskScheduler::~TaskScheduler()
        {
            // stop() is idempotent, so the destructor may call it too (modules/engine/scheduler/README.md).
            stop();
        }
        /**
         * @brief Returns the accumulated time split for a worker.
         */
        TaskScheduler::WorkerTimeSplit
        TaskScheduler::worker_times(int worker_id) const noexcept
        {
            WorkerTimeSplit s;
            if (worker_id < 0 || static_cast<size_t>(worker_id) >= num_workers_)
                return s;
            const auto& b = worker_time_[worker_id];
            s.busy_ns     = b.busy_ns.load(std::memory_order_relaxed);
            s.stealing_ns = b.stealing_ns.load(std::memory_order_relaxed);
            s.polling_ns  = b.polling_ns.load(std::memory_order_relaxed);
            s.idle_ns     = b.idle_ns.load(std::memory_order_relaxed);
            return s;
        }
        /**
         * @brief Zeroes every worker's accumulated time split.
         */
        void TaskScheduler::reset_worker_times() noexcept
        {
            for (size_t i = 0; i < K_MAX_WORKER_THREADS; ++i)
            {
                worker_time_[i].busy_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].stealing_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].polling_ns.store(0, std::memory_order_relaxed);
                worker_time_[i].idle_ns.store(0, std::memory_order_relaxed);
            }
        }
        /** 
         * @brief Submits a new task to the main thread's queue. 
         * @param task Pointer to the task to submit.
         */
        void TaskScheduler::submit(Task* task) 
        {
            if (!task) return;

            task->add_ref();

            // The task is counted before workers can reach it (modules/engine/scheduler/README.md).
            quiescence_.add();

            // External submissions use the injector, not a worker deque (modules/engine/scheduler/README.md).
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                if (task->priority() > 0)
                {
                    priority_submissions.push(task);
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                }
                else
                {
                    pending_submissions.push_back(task);
                    pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);
                }
            }

            // Wake a worker up if they are heavily sleeping
            pending_cv.notify_one();
            post_rounds();
        }
        /**
         * @brief Submits a batch of root nodes under one injector pass (modules/engine/scheduler/README.md).
         */
        void TaskScheduler::submit_batch(const std::vector<Internal::Graph::Node*>& roots)
        {
            if (roots.empty()) return;

            size_t normal_added = 0;
            size_t priority_added = 0;
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                for (Internal::Graph::Node* node : roots)
                {
                    if (!node) continue;
                    Task* task = node; // Node derives from Task
                    task->add_ref();
                    if (task->priority() > 0)
                    {
                        priority_submissions.push(task);
                        ++priority_added;
                    }
                    else
                    {
                        pending_submissions.push_back(task);
                        ++normal_added;
                    }
                }
                quiescence_.add(normal_added + priority_added);
                if (normal_added)
                    pending_count_.store(pending_submissions.size(), std::memory_order_relaxed);
                if (priority_added)
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
            }

            // One notify_all wakes the pool for the whole batch (modules/engine/scheduler/README.md).
            pending_cv.notify_all();
            post_rounds();
        }
        /**
         * @brief Blocks until every task counted into the run has been reaped or offloaded.
         *
         * @see modules/engine/scheduler/README.md
         */
        void TaskScheduler::wait_quiescent()
        {
            quiescence_.wait();
        }
        /**
         * @brief Gets the number of worker threads.
         * @return The number of workers.
         */
        size_t TaskScheduler::worker_count() const 
        {
            return num_workers_;
        }
        /**
         * @brief Gets the current size of a worker's local queue.
         * @param worker_id The ID of the worker.
         * @return The number of tasks in the worker's queue.
         */
        size_t TaskScheduler::queue_size(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            if (worker_queues[worker_id]) return worker_queues[worker_id]->size();
            return 0;
        }
        /**
         * @brief Gets the total number of tasks completed by a worker.
         * @param worker_id The ID of the worker.
         * @return The number of completed tasks.
         */
        size_t TaskScheduler::completed_task_count(int worker_id) const 
        {
            if (worker_id < 0 || worker_id >= static_cast<int>(num_workers_)) return 0;
            return worker_completed_tasks[worker_id].value.load(std::memory_order_relaxed);
        }
        /**
         * @brief Moves tasks from a congested worker onto the global priority queue.
         * @param from_worker_id The ID of the heavily loaded worker.
         * @param count The number of tasks to migrate.
         */
        void TaskScheduler::migrate_tasks(int from_worker_id, size_t count) 
        {
            if (from_worker_id < 0 || from_worker_id >= static_cast<int>(num_workers_)) return;
            if (!worker_queues[from_worker_id]) return;

            // 1. Steal tasks lock-free from the congested (hot) worker.
            std::vector<Task*> rescued_tasks;
            rescued_tasks.reserve(count);

            for (size_t i = 0; i < count; ++i) 
            {
                Task* t = worker_queues[from_worker_id]->steal();
                if (t) rescued_tasks.push_back(t);
                else break; // Stop once the queue is empty.
            }

            // 2. Put the stolen tasks on the global priority queue for cooler workers.
            if (!rescued_tasks.empty()) 
            {
                {
                    SR_LOCK_WAIT("lock.pending", pending_mutex);
                    for (Task* t : rescued_tasks)
                    {
                        priority_submissions.push(t);
                    }
                    priority_count_.store(priority_submissions.size(), std::memory_order_relaxed);
                }
                // Wake every sleeping or idle cool worker.
                pending_cv.notify_all(); 
            }
        }
        /**
         * @brief Installs the offload coordinator in the scheduler and its poller, and binds it.
         * @param offload Not owned; null removes the installed one.
         */
        void TaskScheduler::set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload)
        {
            // The coordinator holds its sink before a worker can offer it a node.
            if (offload != nullptr)
                offload->bind(*this);

            offload_gate_.install(offload);
            event_poller.set_offload_coordinator(offload);
        }

        void TaskScheduler::prepare_offload(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                                            const std::shared_ptr<const PlacementRules>& rules)
        {
            offload_gate_.prepare(nodes, rules);
        }
    } // namespace Scheduler
} // namespace SushiRuntime