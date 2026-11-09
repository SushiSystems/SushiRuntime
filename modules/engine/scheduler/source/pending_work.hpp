/**************************************************************************/
/* pending_work.hpp                                                       */
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
 * @file pending_work.hpp
 * @brief Declares PendingWork, the injector queues of the worker pool and the signal that wakes it.
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <vector>

#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Internal::Scheduler
    {
        /**
         * @brief Orders the global priority queue so its top is the highest-priority task.
         *
         * Returns true when @p a has a lower priority than @p b, which std::priority_queue
         * reads as "a comes out after b". A null pointer compares below every task.
         */
        struct TaskPriorityCompare
        {
            bool operator()(const Task* a, const Task* b) const
            {
                if (!a) return true;
                if (!b) return false;
                return a->priority() < b->priority();
            }
        };

        /**
         * @brief Implements a task FIFO that does not allocate in the steady state.
         * @see modules/engine/scheduler/README.md
         */
        class SubmissionQueue
        {
            public:
                /** @brief Returns whether the queue holds no task. */
                bool empty() const noexcept { return head_ >= items_.size(); }

                /** @brief Returns how many tasks the queue holds. */
                std::size_t size() const noexcept { return items_.size() - head_; }

                /** @brief Returns the oldest task. */
                Task* front() const noexcept { return items_[head_]; }

                /** @brief Appends @p task. */
                void push_back(Task* task) { items_.push_back(task); }

                /** @brief Drops the oldest task, rewinding or compacting the buffer when it can. */
                void pop_front() noexcept
                {
                    ++head_;
                    if (head_ >= items_.size())
                    {
                        // Drained: rewind into the same buffer; clear() keeps capacity.
                        items_.clear();
                        head_ = 0;
                    }
                    else if (head_ >= K_COMPACT_AFTER && head_ * 2 >= items_.size())
                    {
                        // The consumed prefix is dropped by a move inside the same buffer.
                        items_.erase(items_.begin(), items_.begin() + static_cast<std::ptrdiff_t>(head_));
                        head_ = 0;
                    }
                }

            private:
                static constexpr std::size_t K_COMPACT_AFTER = 1024;
                std::vector<Task*> items_;
                std::size_t head_ = 0;
        };

        /**
         * @brief Holds the tasks submitted from outside the pool and the flags that park or wake it.
         *
         * Every member is safe to call from several threads at once.
         * @see modules/engine/scheduler/README.md
         */
        class PendingWork final
        {
            public:
                /** @brief Builds the queues empty; the lock waits record into @p collector, which must outlive them. */
                explicit PendingWork(Profile::Collector& collector) noexcept : collector_(collector) {}


                PendingWork(const PendingWork&) = delete;
                PendingWork& operator=(const PendingWork&) = delete;
                PendingWork(PendingWork&&) = delete;
                PendingWork& operator=(PendingWork&&) = delete;
                ~PendingWork() = default;

                /** @brief Queues @p task, by priority, and wakes one worker; the caller holds the task's reference. */
                void push(Task* task);

                /** @brief Takes a reference on each of @p roots, queues them, counts them in @p quiescence and wakes the pool. */
                void push_roots(const std::vector<Internal::Graph::Node*>& roots, QuiescenceLatch& quiescence);

                /** @brief Queues tasks taken back from a worker's deque on the priority queue and wakes the pool. */
                void push_rescued(const std::vector<Task*>& tasks);

                /** @brief Moves up to @p limit queued tasks into @p batch, which it clears first. */
                void take_batch(std::vector<Task*>& batch, std::size_t limit);

                /** @brief Removes and returns the highest-priority queued task, or null when there is none. */
                Task* take_priority();

                /** @brief Returns whether any queue holds a task, read without the lock. */
                bool has_any() const noexcept;

                /** @brief Returns whether the normal queue holds a task, read without the lock. */
                bool has_normal() const noexcept;

                /** @brief Returns whether the priority queue holds a task, read without the lock. */
                bool has_priority() const noexcept;

                /** @brief Marks a run armed and wakes the pool. */
                void arm();

                /** @brief Marks the run over. */
                void disarm() noexcept;

                /** @brief Asks the pool to drain and end, and wakes it. */
                void request_shutdown();

                /** @brief Returns whether a run is armed. */
                bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

                /** @brief Returns whether shutdown was asked. */
                bool is_shutdown() const noexcept { return shutdown_requested_.load(std::memory_order_acquire); }

                /** @brief Blocks until a run is armed, shutdown is asked or a task is queued. */
                void wait_for_work();

                /** @brief Releases every queued task and empties both queues. */
                void release_all();

            private:
                Profile::Collector& collector_;
                SubmissionQueue normal_;
                std::priority_queue<Task*, std::vector<Task*>, TaskPriorityCompare> priority_;
                std::mutex mutex_;
                std::condition_variable cv_;
                std::atomic<std::size_t> normal_count_{0};
                std::atomic<std::size_t> priority_count_{0};
                std::atomic<bool> running_{false};
                std::atomic<bool> shutdown_requested_{false};
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
