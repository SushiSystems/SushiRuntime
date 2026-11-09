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

#include <cstddef>
#include <memory>
#include <new>
#include <thread>
#include <vector>
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

#include "task_scheduler_parts.hpp"
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/scheduler/steal_order.hpp>
#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>
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
         * @brief Sizes the worker pool and builds its steal order; starts no thread (modules/engine/scheduler/README.md).
         */
        TaskScheduler::TaskScheduler(const WorkerPoolSettings& pool,
                                     const Internal::Topology::TopologyView& topology,
                                     Internal::Device::DeviceQueueSet& devices,
                                     const Internal::Memory::USMAllocator& memory,
                                     Profile::Collector& collector)
            : parts_(std::make_unique<Parts>(pool, topology, devices, memory, collector))
        {
            parts_->run.poller.set_inflight_tracker(&parts_->run.quiescence);

            if (parts_->pinning == PinPolicy::Pin)
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} workers pinned to cores [{}, {}).",
                            parts_->rounds.worker_count(), parts_->size.first_core,
                            parts_->size.first_core + parts_->rounds.worker_count());
            }
            else
            {
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "Task scheduler initialized with {} unpinned workers.",
                            parts_->rounds.worker_count());
            }
        }

        /**
         * @brief Destroys the TaskScheduler.
         * Ensures that the scheduler is stopped before destruction.
         */
        TaskScheduler::~TaskScheduler()
        {
            stop();
        }

        std::size_t TaskScheduler::first_core() const noexcept
        {
            return parts_->size.first_core;
        }

        PinPolicy TaskScheduler::pinning() const noexcept
        {
            return parts_->pinning;
        }

        void TaskScheduler::set_executor(Executor* executor) noexcept
        {
            parts_->executor_rounds.set_executor(executor);
        }

        void TaskScheduler::start()
        {
            bool expected = false;
            if (!parts_->started.compare_exchange_strong(expected, true))
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "TaskScheduler::start() called more than once; ignoring.");
                return;
            }
            // With an executor, no thread starts: the rounds are jobs.
            if (parts_->executor_rounds.attached())
            {
                parts_->post_rounds();
                return;
            }
            parts_->threads.start();
        }

        void TaskScheduler::stop()
        {
            parts_->pending.request_shutdown();
            parts_->threads.join();

            // Rounds posted to an executor run to their end here, so the queues below are empty of live tasks.
            if (parts_->executor_rounds.attached() && parts_->started.load(std::memory_order_acquire))
                parts_->executor_rounds.drain();

            parts_->rounds.release_queues();
            parts_->pending.release_all();
        }

        void TaskScheduler::set_profiling_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept
        {
            parts_->run.ledger.store(ledger, std::memory_order_release);
            parts_->run.poller.set_ledger(ledger);
        }

        void TaskScheduler::arm_for_execution(bool profiling)
        {
            parts_->run.outcome.clear_failure();
            parts_->run.poller.set_profiling(profiling);
            parts_->run.profiling.store(profiling, std::memory_order_release);
            parts_->pending.arm();
            parts_->post_rounds();
        }

        void TaskScheduler::disarm() noexcept
        {
            set_profiling_ledger(nullptr);
            parts_->run.log_run_tail();
            parts_->pending.disarm();
        }

        void TaskScheduler::reset_dispatch_step() noexcept
        {
            parts_->run.next_step.store(0, std::memory_order_release);
        }

        void TaskScheduler::advance_dispatch_step() noexcept
        {
            parts_->run.current_step.store(parts_->run.next_step.fetch_add(1, std::memory_order_acq_rel),
                                           std::memory_order_release);
        }

        std::uint32_t TaskScheduler::current_dispatch_step() const noexcept
        {
            return parts_->run.current_step.load(std::memory_order_acquire);
        }

        void TaskScheduler::submit(Task* task)
        {
            if (!task) return;

            // The reference and the count are taken before a worker can see the task.
            task->add_ref();
            parts_->run.quiescence.add();
            parts_->pending.push(task);
            parts_->post_rounds();
        }

        void TaskScheduler::submit_batch(const std::vector<Internal::Graph::Node*>& roots)
        {
            if (roots.empty()) return;

            parts_->pending.push_roots(roots, parts_->run.quiescence);
            parts_->post_rounds();
        }

        void TaskScheduler::wait_quiescent()
        {
            parts_->run.quiescence.wait();
        }

        bool TaskScheduler::run_failed() const
        {
            return parts_->run.outcome.has_failed();
        }

        std::optional<Error> TaskScheduler::run_error() const
        {
            return parts_->run.outcome.error();
        }

        void TaskScheduler::request_cancellation() noexcept
        {
            parts_->run.outcome.cancel();
        }

        bool TaskScheduler::run_cancelled() const
        {
            return parts_->run.outcome.is_cancelled();
        }

        void TaskScheduler::reset_cancellation()
        {
            parts_->run.outcome.clear_cancellation();
        }

        std::size_t TaskScheduler::worker_count() const
        {
            return parts_->rounds.worker_count();
        }

        std::size_t TaskScheduler::queue_size(int worker_id) const
        {
            return parts_->rounds.queue_size(worker_id);
        }

        std::size_t TaskScheduler::completed_task_count(int worker_id) const
        {
            return parts_->rounds.completed_count(worker_id);
        }

        void TaskScheduler::migrate_tasks(int from_worker_id, std::size_t count)
        {
            parts_->rounds.migrate(from_worker_id, count);
        }

        std::vector<int> TaskScheduler::steal_victim_order(int worker_id) const
        {
            return parts_->rounds.steal_victim_order(worker_id);
        }

        std::size_t TaskScheduler::local_steal_count() const
        {
            return parts_->rounds.local_steals();
        }

        std::size_t TaskScheduler::remote_steal_count() const
        {
            return parts_->rounds.remote_steals();
        }

        TaskScheduler::WorkerTimeSplit TaskScheduler::worker_times(int worker_id) const noexcept
        {
            const WorkerRounds::TimeSplit split = parts_->rounds.time_split(worker_id);
            WorkerTimeSplit result;
            result.busy_ns = split.busy_ns;
            result.stealing_ns = split.stealing_ns;
            result.polling_ns = split.polling_ns;
            result.idle_ns = split.idle_ns;
            return result;
        }

        void TaskScheduler::reset_worker_times() noexcept
        {
            parts_->rounds.reset_times();
        }

        void TaskScheduler::set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload)
        {
            if (offload != nullptr)
                offload->bind(*this);
            parts_->run.offload_gate.install(offload);
            parts_->run.poller.set_offload_coordinator(offload);
        }

        std::uint64_t TaskScheduler::offloaded_task_count() const noexcept
        {
            return parts_->run.offload_gate.accepted_count();
        }

        void TaskScheduler::prepare_offload(span<const sushi_ptr<Internal::Graph::Node>> nodes,
                                            const std::shared_ptr<const PlacementRules>& rules)
        {
            parts_->run.offload_gate.prepare(nodes, rules);
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
