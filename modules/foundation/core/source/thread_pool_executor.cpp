/**************************************************************************/
/* thread_pool_executor.cpp                                               */
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
 * @file thread_pool_executor.cpp
 * @brief Defines ThreadPoolExecutor, which runs posted jobs on threads it owns.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#include <SushiRuntime/core/thread_pool_executor.hpp>

#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>

namespace SushiRuntime
{
    ThreadPoolExecutor::ThreadPoolExecutor(std::size_t thread_count)
    {
        workers_.reserve(thread_count);
        for (std::size_t index = 0; index < thread_count; ++index)
            workers_.emplace_back([this]() { work(); });
    }

    ThreadPoolExecutor::~ThreadPoolExecutor()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            pending_.clear();
        }
        has_work_.notify_all();
        for (std::thread& worker : workers_)
            worker.join();
    }

    void ThreadPoolExecutor::post(Job job)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(job));
        }
        has_work_.notify_one();
    }

    bool ThreadPoolExecutor::run_one()
    {
        Job job;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.empty())
                return false;
            job = std::move(pending_.front());
            pending_.pop_front();
            ++running_;
        }
        run_taken(job);
        return true;
    }

    void ThreadPoolExecutor::wait_idle()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this]() { return pending_.empty() && running_ == 0; });
    }

    void ThreadPoolExecutor::work()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                has_work_.wait(lock, [this]() { return stopping_ || !pending_.empty(); });
                if (stopping_)
                    return;
                job = std::move(pending_.front());
                pending_.pop_front();
                ++running_;
            }
            run_taken(job);
        }
    }

    void ThreadPoolExecutor::run_taken(Job& job)
    {
        job();
        bool idle = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            --running_;
            idle = running_ == 0 && pending_.empty();
        }
        if (idle)
            idle_.notify_all();
    }
} // namespace SushiRuntime
