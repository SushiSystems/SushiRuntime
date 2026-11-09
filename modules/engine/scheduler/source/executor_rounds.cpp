/**************************************************************************/
/* executor_rounds.cpp                                                    */
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
 * @file executor_rounds.cpp
 * @brief Implements ExecutorRounds.
 * @author Mustafa Garip
 */

#include "executor_rounds.hpp"

#include <memory>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        ExecutorRounds::ExecutorRounds(WorkerRounds& rounds)
            : rounds_(rounds), posted_(std::make_unique<std::atomic<bool>[]>(rounds.worker_count()))
        {
        }

        void ExecutorRounds::set_executor(Executor* executor) noexcept
        {
            executor_ = executor;
        }

        void ExecutorRounds::post_round(int thread_id)
        {
            bool expected = false;
            if (!posted_[thread_id].compare_exchange_strong(expected, true))
                return;

            std::weak_ptr<int> alive = alive_;
            executor_->post([this, alive, thread_id]()
            {
                if (alive.expired())
                    return;
                const WorkerRounds::Round round = rounds_.run_round(thread_id);
                posted_[thread_id].store(false);
                const bool more = round == WorkerRounds::Round::Ran || rounds_.has_work(thread_id);
                if (more && round != WorkerRounds::Round::Exit)
                    post_round(thread_id);
            });
        }

        void ExecutorRounds::post_rounds()
        {
            for (std::size_t i = 0; i < rounds_.worker_count(); ++i)
                post_round(static_cast<int>(i));
        }

        void ExecutorRounds::drain()
        {
            for (std::size_t i = 0; i < rounds_.worker_count(); ++i)
                while (rounds_.run_round(static_cast<int>(i)) != WorkerRounds::Round::Exit)
                {
                }
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
