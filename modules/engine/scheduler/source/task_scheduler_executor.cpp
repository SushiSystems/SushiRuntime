/**************************************************************************/
/* task_scheduler_executor.cpp                                           */
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
 * @file task_scheduler_executor.cpp
 * @brief Runs the worker pool rounds as jobs of an Executor.
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

        /**
         * @brief Runs the rounds as executor jobs from now on (modules/engine/scheduler/README.md).
         */
        void TaskScheduler::set_executor(Executor* executor) noexcept
        {
            executor_ = executor;
        }
        /**
         * @brief Posts one round of a worker; the round posts the next while the worker has work.
         */
        void TaskScheduler::post_round(int thread_id)
        {
            bool expected = false;
            if (!round_posted_[thread_id].compare_exchange_strong(expected, true))
                return;

            std::weak_ptr<int> alive = alive_;
            executor_->post([this, alive, thread_id]()
            {
                if (alive.expired())
                    return;
                const WorkerRound round = run_round(thread_id);
                round_posted_[thread_id].store(false);

                // A worker with events in flight polls again; one left idle waits for a submit.
                const bool more = round == WorkerRound::Ran
                    || !worker_events_[thread_id]->is_empty()
                    || has_pending_submissions()
                    || worker_queues[thread_id]->size() != 0;
                if (more && round != WorkerRound::Exit)
                    post_round(thread_id);
            });
        }
        /**
         * @brief Posts a round of every worker when the scheduler runs on an executor.
         */
        void TaskScheduler::post_rounds()
        {
            if (!executor_ || !threads_started_.load(std::memory_order_acquire))
                return;
            for (size_t i = 0; i < num_workers_; ++i)
                post_round(static_cast<int>(i));
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
