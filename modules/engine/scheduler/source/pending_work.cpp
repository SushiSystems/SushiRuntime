/**************************************************************************/
/* pending_work.cpp                                                       */
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
 * @file pending_work.cpp
 * @brief Implements PendingWork.
 * @author Mustafa Garip
 */

#include "pending_work.hpp"

#include <algorithm>

#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        void PendingWork::push(Task* task)
        {
            {
                SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
                if (task->priority() > 0)
                {
                    priority_.push(task);
                    priority_count_.store(priority_.size(), std::memory_order_relaxed);
                }
                else
                {
                    normal_.push_back(task);
                    normal_count_.store(normal_.size(), std::memory_order_relaxed);
                }
            }
            cv_.notify_one();
        }

        void PendingWork::push_roots(const std::vector<Internal::Graph::Node*>& roots, QuiescenceLatch& quiescence)
        {
            std::size_t normal_added = 0;
            std::size_t priority_added = 0;
            {
                SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
                for (Internal::Graph::Node* node : roots)
                {
                    if (!node) continue;
                    Task* task = node; // Node derives from Task
                    task->add_ref();
                    if (task->priority() > 0)
                    {
                        priority_.push(task);
                        ++priority_added;
                    }
                    else
                    {
                        normal_.push_back(task);
                        ++normal_added;
                    }
                }
                // Counted under the lock, so a worker cannot retire a root before it is counted.
                quiescence.add(normal_added + priority_added);
                if (normal_added)
                    normal_count_.store(normal_.size(), std::memory_order_relaxed);
                if (priority_added)
                    priority_count_.store(priority_.size(), std::memory_order_relaxed);
            }
            cv_.notify_all();
        }

        void PendingWork::push_rescued(const std::vector<Task*>& tasks)
        {
            {
                SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
                for (Task* task : tasks)
                    priority_.push(task);
                priority_count_.store(priority_.size(), std::memory_order_relaxed);
            }
            cv_.notify_all();
        }

        void PendingWork::take_batch(std::vector<Task*>& batch, std::size_t limit)
        {
            batch.clear();
            SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
            if (normal_.empty()) return;
            const std::size_t grab_count = std::min(limit, normal_.size());
            batch.reserve(grab_count);
            for (std::size_t i = 0; i < grab_count; ++i)
            {
                batch.push_back(normal_.front());
                normal_.pop_front();
            }
            normal_count_.store(normal_.size(), std::memory_order_relaxed);
            // Others may still find work.
            if (!normal_.empty())
                cv_.notify_one();
        }

        Task* PendingWork::take_priority()
        {
            SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
            if (priority_.empty())
                return nullptr;
            Task* task = priority_.top();
            priority_.pop();
            priority_count_.store(priority_.size(), std::memory_order_relaxed);
            return task;
        }

        bool PendingWork::has_any() const noexcept
        {
            return normal_count_.load(std::memory_order_relaxed) != 0
                || priority_count_.load(std::memory_order_relaxed) != 0;
        }

        bool PendingWork::has_normal() const noexcept
        {
            return normal_count_.load(std::memory_order_relaxed) != 0;
        }

        bool PendingWork::has_priority() const noexcept
        {
            return priority_count_.load(std::memory_order_relaxed) != 0;
        }

        void PendingWork::arm()
        {
            {
                SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
                running_.store(true, std::memory_order_release);
            }
            cv_.notify_all();
        }

        void PendingWork::disarm() noexcept
        {
            SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
            running_.store(false, std::memory_order_release);
        }

        void PendingWork::request_shutdown()
        {
            {
                SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
                shutdown_requested_.store(true, std::memory_order_release);
                running_.store(false, std::memory_order_release);
            }
            cv_.notify_all();
        }

        void PendingWork::wait_for_work()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]()
            {
                return running_.load(std::memory_order_acquire) ||
                       shutdown_requested_.load(std::memory_order_acquire) ||
                       !normal_.empty() || !priority_.empty();
            });
        }

        void PendingWork::release_all()
        {
            SR_LOCK_WAIT(collector_, "lock.pending", mutex_);
            while (!normal_.empty())
            {
                Task* task = normal_.front();
                normal_.pop_front();
                if (task)
                    task->release();
            }
            while (!priority_.empty())
            {
                Task* task = priority_.top();
                priority_.pop();
                if (task)
                    task->release();
            }
            normal_count_.store(0, std::memory_order_relaxed);
            priority_count_.store(0, std::memory_order_relaxed);
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
