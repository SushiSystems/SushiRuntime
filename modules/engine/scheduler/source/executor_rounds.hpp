/**************************************************************************/
/* executor_rounds.hpp                                                    */
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
 * @file executor_rounds.hpp
 * @brief Declares ExecutorRounds, the executor shell that runs the worker rounds as jobs of an Executor.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include <SushiRuntime/core/executor.hpp>

#include "worker_rounds.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Posts one round of each worker to an Executor and posts the next while a worker has more to do.
         * @see modules/engine/scheduler/README.md
         */
        class ExecutorRounds final
        {
            public:
                /** @brief Holds the rounds it posts; @p rounds must outlive the shell. */
                explicit ExecutorRounds(WorkerRounds& rounds);

                ExecutorRounds(const ExecutorRounds&) = delete;
                ExecutorRounds& operator=(const ExecutorRounds&) = delete;
                ExecutorRounds(ExecutorRounds&&) = delete;
                ExecutorRounds& operator=(ExecutorRounds&&) = delete;

                /** @brief Expires the rounds still posted, so one that runs after the shell is gone does nothing. */
                ~ExecutorRounds() = default;

                /** @brief Sets the executor the rounds are posted to; null selects none. Not owned. */
                void set_executor(Executor* executor) noexcept;

                /** @brief Returns whether an executor is set. */
                bool attached() const noexcept { return executor_ != nullptr; }

                /** @brief Posts a round of every worker that has none posted; requires an executor. */
                void post_rounds();

                /** @brief Runs rounds of every worker on the calling thread until each one reports Exit. */
                void drain();

            private:
                /** @brief Posts one round of worker @p thread_id unless one is posted already. */
                void post_round(int thread_id);

                WorkerRounds& rounds_;
                Executor* executor_ = nullptr;
                std::unique_ptr<std::atomic<bool>[]> posted_;
                std::shared_ptr<int> alive_ = std::make_shared<int>(0);
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
