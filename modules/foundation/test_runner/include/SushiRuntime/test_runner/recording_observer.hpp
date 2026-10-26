/**************************************************************************/
/* recording_observer.hpp                                                 */
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
 * @file recording_observer.hpp
 * @brief Declares RecordingObserver, a RunObserver that keeps every observation a test receives.
 * @author Mustafa Garip
 * @see modules/foundation/test_runner/README.md
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime::TestRunner
{
    /**
     * @brief Copies every observation it receives, for a test to read after the run.
     *
     * Thread-safe: on_run() runs on the thread that learned the run finished. Add it to a runtime
     * with Runtime::add_run_observer() and keep the handle for as long as the test reads runs.
     * @see modules/foundation/test_runner/README.md
     */
    class RecordingObserver final : public RunObserver
    {
        public:
            /** @brief Stores a copy of @p observation. */
            void on_run(const RunObservation& observation) noexcept override
            {
                try
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    runs_.push_back(observation);
                }
                catch (...)
                {
                    // A test that loses an observation fails on its count.
                }
            }

            /** @brief Returns how many runs were observed. */
            [[nodiscard]] std::size_t count() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return runs_.size();
            }

            /** @brief Returns a copy of every observation, oldest first. */
            [[nodiscard]] std::vector<RunObservation> runs() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return runs_;
            }

            /**
             * @brief Returns a copy of the latest observation.
             * @pre count() is not 0.
             */
            [[nodiscard]] RunObservation last() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return runs_.back();
            }

            /** @brief Forgets every observation. */
            void clear()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                runs_.clear();
            }

        private:
            /** @brief Guards runs_. */
            mutable std::mutex mutex_;

            /** @brief The observations, oldest first. */
            std::vector<RunObservation> runs_;
    };
} // namespace SushiRuntime::TestRunner
