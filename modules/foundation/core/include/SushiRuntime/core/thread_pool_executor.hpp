/**************************************************************************/
/* thread_pool_executor.hpp                                               */
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
 * @file thread_pool_executor.hpp
 * @brief Declares ThreadPoolExecutor, the Executor that runs posted jobs on threads it owns.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    /**
     * @brief Runs posted jobs on a fixed set of threads, in the order they were posted.
     *
     * run_one() lets a caller run a pending job on its own thread beside the workers. Any thread
     * may call every member but the destructor.
     * @see modules/foundation/core/README.md
     */
    class SUSHIRUNTIME_API ThreadPoolExecutor final : public Executor
    {
        public:
            /**
             * @brief Starts the worker threads.
             * @param thread_count The number of workers; 0 runs jobs only inside run_one().
             */
            explicit ThreadPoolExecutor(std::size_t thread_count);

            ThreadPoolExecutor(const ThreadPoolExecutor&) = delete;
            ThreadPoolExecutor& operator=(const ThreadPoolExecutor&) = delete;
            ThreadPoolExecutor(ThreadPoolExecutor&&) = delete;
            ThreadPoolExecutor& operator=(ThreadPoolExecutor&&) = delete;

            /** @brief Waits for the jobs that are running, drops the pending ones and joins the workers. */
            ~ThreadPoolExecutor() override;

            /** @brief Adds a job and wakes one worker. */
            void post(Job job) override;

            /** @brief Runs the oldest pending job on the calling thread. */
            [[nodiscard]] bool run_one() override;

            /** @brief Blocks until no job is pending or running, the ones jobs post included. */
            void wait_idle();

        private:
            /** @brief Runs pending jobs until the destructor stops the pool. */
            void work();

            /** @brief Runs a job taken from the queue and counts it finished. */
            void run_taken(Job& job);

            /** @brief Guards every member below but the threads. */
            std::mutex mutex_;

            /** @brief Wakes a worker when a job is posted or the pool stops. */
            std::condition_variable has_work_;

            /** @brief Wakes wait_idle() when the last running job ends. */
            std::condition_variable idle_;

            /** @brief The jobs posted and not yet taken, oldest first. */
            std::deque<Job> pending_;

            /** @brief The number of jobs taken and not yet finished. */
            std::size_t running_ = 0;

            /** @brief Set by the destructor; a worker that sees it returns. */
            bool stopping_ = false;

            /** @brief The worker threads. */
            std::vector<std::thread> workers_;
    };
} // namespace SushiRuntime
