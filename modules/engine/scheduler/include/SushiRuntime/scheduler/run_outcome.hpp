/**************************************************************************/
/* run_outcome.hpp                                                        */
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
 * @file run_outcome.hpp
 * @brief Declares RunOutcome, the failure and the cancellation of one run.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <mutex>
#include <utility>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Holds the first failure of a run and whether the run was cancelled.
         * @see modules/engine/scheduler/README.md
         */
        class RunOutcome final
        {
            public:
                RunOutcome() = default;
                RunOutcome(const RunOutcome&) = delete;
                RunOutcome& operator=(const RunOutcome&) = delete;
                RunOutcome(RunOutcome&&) = delete;
                RunOutcome& operator=(RunOutcome&&) = delete;
                ~RunOutcome() = default;

                /**
                 * @brief Records the first failure of the run; a later one is dropped.
                 * @param error What a task or the poller failed with; its code is not Ok.
                 */
                void record(Error error)
                {
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    if (!failed_.load(std::memory_order_relaxed))
                    {
                        error_ = std::move(error);
                        failed_.store(true, std::memory_order_release);
                    }
                }

                /** @brief Returns the recorded failure; its code is Ok when the run did not fail. */
                [[nodiscard]] Error error() const
                {
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    return error_;
                }

                /** @brief Tells whether a task failed during the run. */
                [[nodiscard]] bool has_failed() const noexcept
                {
                    return failed_.load(std::memory_order_acquire);
                }

                /** @brief Forgets the failure of the round before. */
                void clear_failure()
                {
                    {
                        std::lock_guard<std::mutex> lock(error_mutex_);
                        error_ = Error{};
                    }
                    failed_.store(false, std::memory_order_release);
                }

                /** @brief Marks the run cancelled; any thread may call it. */
                void cancel() noexcept
                {
                    cancelled_.store(true, std::memory_order_release);
                }

                /** @brief Tells whether cancel() was called and not yet cleared. */
                [[nodiscard]] bool is_cancelled() const noexcept
                {
                    return cancelled_.load(std::memory_order_acquire);
                }

                /** @brief Clears the cancellation at the start of a fresh run. */
                void clear_cancellation() noexcept
                {
                    cancelled_.store(false, std::memory_order_release);
                }

                /** @brief Tells whether the run failed or was cancelled, so no more kernels run. */
                [[nodiscard]] bool is_aborting() const noexcept
                {
                    return has_failed() || is_cancelled();
                }

            private:
                /// @brief Whether record() has stored a failure this round.
                std::atomic<bool> failed_{false};

                /// @brief Whether cancel() was called this run.
                std::atomic<bool> cancelled_{false};

                /// @brief Guards error_ on the failure path.
                mutable std::mutex error_mutex_;

                /// @brief First failure of the run.
                Error error_;
        };

    } // namespace Scheduler
} // namespace SushiRuntime
