/**************************************************************************/
/* completion_signal.hpp                                                  */
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
 * @file completion_signal.hpp
 * @brief Declares the signal the work under measurement raises when it finishes.
 * @author Mustafa Garip
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Lets the work under measurement tell the measuring thread that it has finished.
         * @pre The signal outlives every thread that may raise it.
         */
        class CompletionSignal final
        {
            public:
                /** @brief Lowers the signal before the next timed run. */
                void reset()
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    raised_ = false;
                }

                /** @brief Raises the signal and wakes the waiting thread. */
                void raise()
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    raised_ = true;
                    raised_changed_.notify_all();
                }

                /**
                 * @brief Blocks until the signal is raised or the budget runs out.
                 * @return True when the signal was raised, false on timeout.
                 */
                [[nodiscard]] bool wait_for(std::chrono::seconds budget)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return raised_changed_.wait_for(lock, budget, [this] { return raised_; });
                }

            private:
                /// @brief Guards raised_.
                std::mutex mutex_;
                /// @brief Wakes the waiter when raised_ turns true.
                std::condition_variable raised_changed_;
                /// @brief Whether the work has reported completion since the last reset().
                bool raised_ = false;
        };
    } // namespace Test
} // namespace SushiRuntime
