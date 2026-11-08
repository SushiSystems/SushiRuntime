/**************************************************************************/
/* task_scheduler_threads.cpp                                            */
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
 * @file task_scheduler_threads.cpp
 * @brief Runs the worker pool on threads of its own: start, stop and the dispatch loop.
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
         * @brief Starts the task scheduler.
         * Spawns worker threads that wait for tasks to be submitted.
         */
        void TaskScheduler::start()
        {
            // Workers are spawned exactly once (modules/engine/scheduler/README.md).
            bool expected = false;
            if (!threads_started_.compare_exchange_strong(expected, true))
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "TaskScheduler::start() called more than once; ignoring.");
                return;
            }

            // On an executor the rounds are jobs, posted once work can be found.
            if (executor_)
            {
                post_rounds();
                return;
            }

            // Workers wait; arm_for_execution() precedes submitted work (modules/engine/scheduler/README.md).
            for(size_t i=0; i<num_workers_; ++i)
            {
                workers[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
            }
        }
        /**
         * @brief Stops the task scheduler.
         * Signals workers to drain remaining tasks and exit, then joins threads.
         */
        void TaskScheduler::stop()
        {
            // The flags change under pending_mutex (modules/engine/scheduler/README.md).
            {
                SR_LOCK_WAIT("lock.pending", pending_mutex);
                shutdown_requested.store(true, std::memory_order_release);
                running.store(false, std::memory_order_release);
            }
            pending_cv.notify_all();
            for (size_t i = 0; i < num_workers_; ++i)
                if (workers[i].joinable()) workers[i].join();

            // On an executor, the drain the threads would run happens here, on the caller.
            if (executor_ && threads_started_.load(std::memory_order_acquire))
            {
                for (size_t i = 0; i < num_workers_; ++i)
                    while (run_round(static_cast<int>(i)) != WorkerRound::Exit)
                    {
                    }
            }

            for (size_t i = 0; i < num_workers_; ++i)
            {
                if (worker_queues[i])
                {
                    while (Task* task = worker_queues[i]->pop())
                        task->release();
                }
            }

            // Release any submissions that never made it into execution.
            SR_LOCK_WAIT("lock.pending", pending_mutex);
            while (!pending_submissions.empty())
            {
                Task* task = pending_submissions.front();
                pending_submissions.pop_front();
                if (task)
                    task->release();
            }

            while (!priority_submissions.empty())
            {
                Task* task = priority_submissions.top();
                priority_submissions.pop();
                if (task)
                    task->release();
            }
            pending_count_.store(0, std::memory_order_relaxed);
            priority_count_.store(0, std::memory_order_relaxed);
        }
        /**
         * @brief Runs a worker's rounds on its thread, parking and backing off between them
         *        (modules/engine/scheduler/README.md).
         * @param thread_id The ID of the worker executing this loop.
         */
        void TaskScheduler::dispatch_loop(int thread_id)
        {
            if (static_cast<size_t>(thread_id) >= num_workers_)
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "Dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            // The thread is registered first, so its first zone allocates nothing.
            Internal::Profile::name_this_thread("sr-worker-" + std::to_string(thread_id));

            // A pinned worker stays on core first_core_ + id (modules/engine/scheduler/README.md).
            if (pinning_ == PinPolicy::Pin)
                SushiRuntime::Internal::Topology::pin_current_thread_to_core(
                    static_cast<int>(first_core_) + thread_id);

            using sclock = std::chrono::steady_clock;
            auto& wt = worker_time_[thread_id];
            const EventPolling::ActiveEventList& active_events = *worker_events_[thread_id];
            auto add_idle = [&wt](sclock::time_point a) noexcept
            {
                wt.idle_ns.fetch_add(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(sclock::now() - a).count()),
                    std::memory_order_relaxed);
            };

            int spin_count = 0;
            while (true)
            {
                const bool prof = profiling_active_.load(std::memory_order_acquire);
                const WorkerRound round = run_round(thread_id);
                if (round == WorkerRound::Exit)
                    break;
                if (round == WorkerRound::Ran)
                {
                    spin_count = 0;
                    continue;
                }

                const auto idle0 = prof ? sclock::now() : sclock::time_point{};
                if (round == WorkerRound::Park)
                {
                    // The park has no timeout (modules/engine/scheduler/README.md).
                    std::unique_lock<std::mutex> lock(pending_mutex);
                    pending_cv.wait(lock, [this]()
                    {
                        return running.load(std::memory_order_acquire) ||
                               shutdown_requested.load(std::memory_order_acquire) ||
                               !pending_submissions.empty() ||
                               !priority_submissions.empty();
                    });
                }
                else
                {
                    // A worker holding in-flight events never sleeps: its successors wait on its poll.
                    ++spin_count;
                    if (spin_count < 100 || !active_events.is_empty())
                        std::this_thread::yield();
                    else
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                if (prof) add_idle(idle0);
            }
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
