/**************************************************************************/
/* worker_rounds.hpp                                                      */
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
 * @file worker_rounds.hpp
 * @brief Declares WorkerRounds, the per-worker queues and the round each worker runs over them.
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/work_stealing/work_stealing_deque.hpp>

#include "pending_work.hpp"
#include "run_state.hpp"

namespace SushiRuntime
{
    namespace Internal::Topology { class TopologyView; }
    namespace Internal::Device { class DeviceQueueSet; }
    namespace Internal::Memory { class USMAllocator; }

    namespace Internal::Scheduler
    {
        /**
         * @brief Owns the worker deques and runs one round of a worker: drain, poll, pick a task, run it.
         *
         * A round never blocks; the thread shell and the executor shell decide what happens between rounds.
         * @see modules/engine/scheduler/README.md
         */
        class WorkerRounds final
        {
            public:
                /** @brief Names what one round of a worker did, so its shell knows what follows. */
                enum class Round : std::uint8_t
                {
                    Ran,  /**< A task ran or was offloaded. */
                    Idle, /**< No task was found; the run is armed or events are in flight. */
                    Park, /**< Nothing is armed, queued or in flight; the worker may sleep. */
                    Exit, /**< Shutdown was asked and nothing is left; the worker ends. */
                };

                /** @brief The time one worker spent in each activity, in nanoseconds. */
                struct TimeSplit
                {
                    std::uint64_t busy_ns = 0;     /**< Inside task execution. */
                    std::uint64_t stealing_ns = 0; /**< Searching victims for work. */
                    std::uint64_t polling_ns = 0;  /**< Polling in-flight events. */
                    std::uint64_t idle_ns = 0;     /**< Backing off or waiting to be armed. */
                };

                /**
                 * @brief Builds a deque and an event list per worker and the NUMA steal order.
                 * @param size The worker count and first pinned core.
                 * @param pinning Whether workers are bound to cores; the steal order follows it.
                 * @param topology Read during construction only.
                 * @param devices,memory,pending,run Must outlive the rounds.
                 */
                WorkerRounds(const PoolSize& size, PinPolicy pinning, const Internal::Topology::TopologyView& topology,
                             Internal::Device::DeviceQueueSet& devices, const Internal::Memory::USMAllocator& memory,
                             PendingWork& pending, RunState& run);

                WorkerRounds(const WorkerRounds&) = delete;
                WorkerRounds& operator=(const WorkerRounds&) = delete;
                WorkerRounds(WorkerRounds&&) = delete;
                WorkerRounds& operator=(WorkerRounds&&) = delete;
                ~WorkerRounds() = default;

                /** @brief Returns the number of workers. */
                std::size_t worker_count() const noexcept { return num_workers_; }

                /** @brief Runs one round of worker @p thread_id; it never blocks or sleeps. */
                Round run_round(int thread_id);

                /** @brief Returns whether worker @p thread_id has events in flight, a queued task or pending work to take. */
                bool has_work(int thread_id) const;

                /** @brief Returns whether worker @p thread_id has no event in flight. */
                bool events_empty(int thread_id) const;

                /** @brief Adds @p nanoseconds to the idle bucket of worker @p thread_id. */
                void add_idle_ns(int thread_id, std::uint64_t nanoseconds) noexcept;

                /** @brief Returns the length of worker @p worker_id's deque, or 0 when the id is out of range. */
                std::size_t queue_size(int worker_id) const;

                /** @brief Returns the tasks worker @p worker_id completed, or 0 when the id is out of range. */
                std::size_t completed_count(int worker_id) const;

                /** @brief Moves up to @p count tasks from worker @p from_worker_id's deque to the priority queue. */
                void migrate(int from_worker_id, std::size_t count);

                /** @brief Returns worker @p worker_id's steal victims, nearest NUMA node first. */
                std::vector<int> steal_victim_order(int worker_id) const;

                /** @brief Returns the successful steals from a node-local victim. */
                std::size_t local_steals() const noexcept { return local_steals_.load(std::memory_order_relaxed); }

                /** @brief Returns the successful steals from a remote-node victim. */
                std::size_t remote_steals() const noexcept { return remote_steals_.load(std::memory_order_relaxed); }

                /** @brief Returns the time split of worker @p worker_id; zeroed when the id is out of range. */
                TimeSplit time_split(int worker_id) const noexcept;

                /** @brief Zeroes every worker's time split. */
                void reset_times() noexcept;

                /** @brief Releases the tasks every deque still holds. */
                void release_queues();

            private:
                /** @brief Holds one worker's completed-task counter on its own cache line. */
                struct alignas(K_CACHE_LINE_SIZE) CompletedCounter
                {
                    std::atomic<std::size_t> value{0};
                };

                /** @brief Holds one worker's time buckets on their own cache line, so the owner is the only writer. */
                struct alignas(K_CACHE_LINE_SIZE) TimeBuckets
                {
                    std::atomic<std::uint64_t> busy_ns{0};
                    std::atomic<std::uint64_t> stealing_ns{0};
                    std::atomic<std::uint64_t> polling_ns{0};
                    std::atomic<std::uint64_t> idle_ns{0};
                };

                /** @brief Executes a single task and tracks its events; @p worker_id is the scheduler's worker id. */
                void execute_task(Task* task, int worker_id, EventPolling::ActiveEventList& events);

                /** @brief Moves pending submissions into the deque of worker @p thread_id. */
                void drain_pending_submissions(int thread_id);

                /** @brief Looks for a task to steal from another worker's deque, node-local victims first. */
                Task* find_task_to_steal(int thread_id);

                std::size_t num_workers_;
                Internal::Device::DeviceQueueSet& devices_;
                const Internal::Memory::USMAllocator& memory_;
                PendingWork& pending_;
                RunState& run_;
                std::array<sushi_ptr<Deque>, K_MAX_WORKER_THREADS> queues_;
                std::array<std::vector<Task*>, K_MAX_WORKER_THREADS> drain_scratch_;
                std::vector<std::unique_ptr<EventPolling::ActiveEventList>> events_;
                std::vector<int> worker_numa_;
                std::vector<std::vector<int>> victim_order_;
                std::vector<int> local_tier_len_;
                std::atomic<std::size_t> local_steals_{0};
                std::atomic<std::size_t> remote_steals_{0};
                CompletedCounter completed_[K_MAX_WORKER_THREADS];
                TimeBuckets worker_time_[K_MAX_WORKER_THREADS];
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
