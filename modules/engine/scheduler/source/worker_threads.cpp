/**************************************************************************/
/* worker_threads.cpp                                                     */
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
 * @file worker_threads.cpp
 * @brief Implements WorkerThreads.
 * @author Mustafa Garip
 */

#include "worker_threads.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/topology/affinity.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        WorkerThreads::WorkerThreads(WorkerRounds& rounds, PendingWork& pending, RunState& run, PinPolicy pinning,
                                     std::size_t first_core) noexcept
            : rounds_(rounds), pending_(pending), run_(run), pinning_(pinning), first_core_(first_core)
        {
        }

        void WorkerThreads::start()
        {
            for (std::size_t i = 0; i < rounds_.worker_count(); ++i)
                workers_[i] = std::thread([this, i] { dispatch_loop(static_cast<int>(i)); });
        }

        void WorkerThreads::join()
        {
            for (std::size_t i = 0; i < rounds_.worker_count(); ++i)
                if (workers_[i].joinable()) workers_[i].join();
        }

        void WorkerThreads::dispatch_loop(int thread_id)
        {
            if (static_cast<std::size_t>(thread_id) >= rounds_.worker_count())
            {
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "Dispatch loop thread_id {} out of bounds!", thread_id);
                return;
            }

            // Registers the thread with the profiler and names it, so its first zone allocates nothing.
            Internal::Profile::name_this_thread("sr-worker-" + std::to_string(thread_id));

            if (pinning_ == PinPolicy::Pin)
                SushiRuntime::Internal::Topology::pin_current_thread_to_core(
                    static_cast<int>(first_core_) + thread_id);

            using sclock = std::chrono::steady_clock;

            int spin_count = 0;
            while (true)
            {
                const bool prof = run_.profiling.load(std::memory_order_acquire);
                const WorkerRounds::Round round = rounds_.run_round(thread_id);
                if (round == WorkerRounds::Round::Exit)
                    break;
                if (round == WorkerRounds::Round::Ran)
                {
                    spin_count = 0;
                    continue;
                }

                const auto idle0 = prof ? sclock::now() : sclock::time_point{};
                if (round == WorkerRounds::Round::Park)
                    pending_.wait_for_work();
                else
                {
                    // Idle: spin briefly, then sleep, unless events are in flight to poll.
                    ++spin_count;
                    if (spin_count < 100 || !rounds_.events_empty(thread_id))
                        std::this_thread::yield();
                    else
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                if (prof)
                    rounds_.add_idle_ns(thread_id, static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(sclock::now() - idle0).count()));
            }
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
