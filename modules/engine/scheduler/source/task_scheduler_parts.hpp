/**************************************************************************/
/* task_scheduler_parts.hpp                                               */
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
 * @file task_scheduler_parts.hpp
 * @brief Declares TaskScheduler::Parts, the pieces a TaskScheduler composes.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <thread>

#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>

#include "executor_rounds.hpp"
#include "pending_work.hpp"
#include "run_state.hpp"
#include "worker_rounds.hpp"
#include "worker_threads.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Composes the pool: the pending work, the run state, the rounds and the two shells that drive them.
         *
         * The members are declared in the order they are built and destroyed in reverse: the shells go first,
         * then the rounds they run over, then the state the rounds read.
         * @see modules/engine/scheduler/README.md
         */
        struct TaskScheduler::Parts
        {
            /** @brief Sizes the pool and builds each piece over the ones before it. */
            Parts(const WorkerPoolSettings& pool, const Internal::Topology::TopologyView& topology,
                  Internal::Device::DeviceQueueSet& devices, const Internal::Memory::USMAllocator& memory,
                  Profile::Collector& collector)
                : size(size_worker_pool(pool, std::thread::hardware_concurrency())),
                  pinning(pool.pinning),
                  run(collector),
                  rounds(size, pinning, topology, devices, memory, pending, run),
                  threads(rounds, pending, run, pinning, size.first_core),
                  executor_rounds(rounds)
            {
            }

            /** @brief Posts a round of every worker when an executor runs them and start() has been called. */
            void post_rounds()
            {
                if (executor_rounds.attached() && started.load(std::memory_order_acquire))
                    executor_rounds.post_rounds();
            }

            const PoolSize size;              /**< The worker count and first pinned core in effect. */
            const PinPolicy pinning;          /**< Whether workers are bound to cores. */
            PendingWork pending;              /**< The injector queues and the run flags. */
            RunState run;                     /**< The state of the active run. */
            WorkerRounds rounds;              /**< The worker deques and their rounds. */
            WorkerThreads threads;            /**< Runs the rounds on OS threads. */
            ExecutorRounds executor_rounds;   /**< Runs the rounds as jobs of an executor. */
            std::atomic<bool> started{false}; /**< Set when start() ran; a later start() returns at once. */
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
