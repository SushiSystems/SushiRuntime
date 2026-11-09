/**************************************************************************/
/* worker_threads.hpp                                                     */
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
 * @file worker_threads.hpp
 * @brief Declares WorkerThreads, the thread shell that runs the worker rounds on OS threads.
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <cstddef>
#include <thread>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>

#include "pending_work.hpp"
#include "run_state.hpp"
#include "worker_rounds.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Runs one thread per worker, each looping over its rounds and parking or backing off between them.
         * @see modules/engine/scheduler/README.md
         */
        class WorkerThreads final
        {
            public:
                /**
                 * @brief Holds what the loops read; starts no thread.
                 * @param rounds Runs each round; must outlive the shell.
                 * @param pending Parks the loops; must outlive the shell.
                 * @param run Tells whether the run profiles; must outlive the shell.
                 * @param pinning Whether each thread is bound to a core.
                 * @param first_core The core worker 0 is bound to, worker i on first_core + i.
                 */
                WorkerThreads(WorkerRounds& rounds, PendingWork& pending, RunState& run, PinPolicy pinning,
                              std::size_t first_core) noexcept;

                WorkerThreads(const WorkerThreads&) = delete;
                WorkerThreads& operator=(const WorkerThreads&) = delete;
                WorkerThreads(WorkerThreads&&) = delete;
                WorkerThreads& operator=(WorkerThreads&&) = delete;

                /** @brief Joins nothing: join() ends the threads before the shell is destroyed. */
                ~WorkerThreads() = default;

                /** @brief Spawns one thread per worker. */
                void start();

                /** @brief Joins every thread that was spawned. */
                void join();

            private:
                /** @brief Runs rounds of worker @p thread_id until shutdown; parks and backs off between them. */
                void dispatch_loop(int thread_id);

                WorkerRounds& rounds_;
                PendingWork& pending_;
                RunState& run_;
                PinPolicy pinning_;
                std::size_t first_core_;
                std::array<std::thread, K_MAX_WORKER_THREADS> workers_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
