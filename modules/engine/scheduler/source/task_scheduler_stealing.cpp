/**************************************************************************/
/* task_scheduler_stealing.cpp                                           */
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
 * @file task_scheduler_stealing.cpp
 * @brief Steals for the worker pool: its steal order and the steal itself.
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
         * @brief Builds the NUMA-aware steal victim table once (modules/engine/scheduler/README.md).
         */
        void TaskScheduler::build_steal_topology()
        {
            StealOrder order = build_steal_order(topology_, num_workers_, pinning_, first_core_);
            worker_numa_ = std::move(order.worker_numa);
            victim_order_ = std::move(order.victim_order);
            local_tier_len_ = std::move(order.local_tier_len);
        }
        /**
         * @brief Steals a task, node-local victims first (modules/engine/scheduler/README.md).
         * @param th_id The ID of the worker attempting to steal.
         * @return Pointer to a stolen task, or nullptr if none found.
         */
        Task* TaskScheduler::find_task_to_steal(int th_id)
        {
            if (th_id < 0 || static_cast<size_t>(th_id) >= num_workers_)
                return nullptr;

            SR_ZONE_DETAIL(collector_, "worker.steal");

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
                    if (Task* stolen = worker_queues[victim_id]->steal())
                    {
                        local_steals_.fetch_add(1, std::memory_order_relaxed);
                        return stolen;
                    }
                }
            }

            // 2. Remote tiers, already sorted by ascending distance — sequential.
            for (int k = local; k < total; ++k)
            {
                if (Task* stolen = worker_queues[order[k]]->steal())
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
        std::vector<int> TaskScheduler::steal_victim_order(int worker_id) const
        {
            if (worker_id < 0 || static_cast<size_t>(worker_id) >= victim_order_.size())
                return {};
            return victim_order_[worker_id];
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
