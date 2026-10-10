/**************************************************************************/
/* quiescence_latch.hpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file quiescence_latch.hpp
 * @brief Declares QuiescenceLatch, the count of tasks a run has not yet reaped.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Counts the tasks submitted for a run and wakes a waiter when the last one leaves.
         * @see modules/engine/scheduler/README.md
         */
        class QuiescenceLatch final
        {
            public:
                QuiescenceLatch() = default;
                QuiescenceLatch(const QuiescenceLatch&) = delete;
                QuiescenceLatch& operator=(const QuiescenceLatch&) = delete;
                QuiescenceLatch(QuiescenceLatch&&) = delete;
                QuiescenceLatch& operator=(QuiescenceLatch&&) = delete;
                ~QuiescenceLatch() = default;

                /**
                 * @brief Counts tasks that entered the run.
                 * @param count Called before a worker can reach any of them.
                 */
                void add(std::size_t count = 1) noexcept
                {
                    inflight_.fetch_add(static_cast<long long>(count), std::memory_order_relaxed);
                }

                /** @brief Counts one task out of the run and wakes wait() on the last one. */
                void retire() noexcept
                {
                    if (inflight_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        // The empty lock orders this wake after a waiter's predicate read.
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                        }
                        drained_.notify_all();
                    }
                }

                /** @brief Blocks until every counted task has retired. */
                void wait()
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    drained_.wait(lock, [this]
                    {
                        return inflight_.load(std::memory_order_acquire) <= 0;
                    });
                }

            private:
                /// @brief Tasks submitted for the run and not yet reaped.
                std::atomic<long long> inflight_{0};

                /// @brief Guards the wait predicate against a lost wakeup.
                std::mutex mutex_;

                /// @brief Signalled when inflight_ reaches zero.
                std::condition_variable drained_;
        };

    } // namespace Scheduler
} // namespace SushiRuntime
