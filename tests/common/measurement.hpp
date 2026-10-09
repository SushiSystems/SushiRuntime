/**************************************************************************/
/* measurement.hpp                                                        */
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
 * @file measurement.hpp
 * @brief Declares the repeated-run measurement, its report line and the signal that ends a run.
 * @author Mustafa Garip
 * @see tests/common/README.md
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace SushiRuntime
{
    namespace Test
    {
        /** @brief Holds the number of timed runs a measurement takes by default. */
        constexpr std::size_t K_DEFAULT_RUN_COUNT = 5;

        /** @brief Holds the token that opens every measurement line on stdout. */
        constexpr const char* K_MEASUREMENT_PREFIX = "SR_MEASUREMENT";

        /**
         * @brief Holds the spread of one measured quantity across its timed runs.
         */
        struct RunStatistics
        {
            double median = 0.0;       /**< Middle sample; mean of the middle two when even. */
            double minimum = 0.0;      /**< Smallest sample. */
            double maximum = 0.0;      /**< Largest sample. */
            std::size_t run_count = 0; /**< Number of samples the three figures summarise. */
        };

        /**
         * @brief Names one measured case for its report line.
         */
        struct MeasurementLabel
        {
            const char* benchmark = "";   /**< Benchmark name; must hold no whitespace. */
            const char* case_name = "";   /**< Case of the benchmark; must hold no whitespace. */
            const char* unit = "";        /**< Unit of the figures; must hold no whitespace. */
            std::size_t worker_count = 0; /**< Scheduler workers the case ran on. */
            std::size_t problem_size = 0; /**< Input size, in the benchmark's own terms. */
        };

        /**
         * @brief Reduces a set of samples to their median, minimum and maximum.
         * @return The statistics of @p samples; all zero when it is empty.
         */
        [[nodiscard]] inline RunStatistics summarize(std::vector<double> samples)
        {
            RunStatistics statistics;
            if (samples.empty())
                return statistics;

            std::sort(samples.begin(), samples.end());
            const std::size_t middle = samples.size() / 2;

            statistics.median = samples.size() % 2 == 1
                                    ? samples[middle]
                                    : (samples[middle - 1] + samples[middle]) / 2.0;
            statistics.minimum = samples.front();
            statistics.maximum = samples.back();
            statistics.run_count = samples.size();
            return statistics;
        }

        /**
         * @brief Calls a timed run repeatedly and summarises the durations it returns.
         * @tparam TimedRun Callable with no arguments that returns one run's figure as a double.
         * @param timed_run Performs one run; it times its own region and keeps setup outside it.
         * @param run_count Number of runs to take.
         */
        template <typename TimedRun>
        [[nodiscard]] RunStatistics measure(TimedRun&& timed_run,
                                            std::size_t run_count = K_DEFAULT_RUN_COUNT)
        {
            std::vector<double> samples;
            samples.reserve(run_count);
            for (std::size_t run = 0; run < run_count; ++run)
                samples.push_back(static_cast<double>(timed_run()));

            return summarize(std::move(samples));
        }

        /**
         * @brief Returns the build type of this translation unit, read from NDEBUG.
         * @return "release" when NDEBUG is defined, "debug" otherwise.
         */
        [[nodiscard]] inline const char* build_type() noexcept
        {
#ifdef NDEBUG
            return "release";
#else
            return "debug";
#endif
        }

        /**
         * @brief Formats one measured case as a single line of space-separated key=value pairs.
         * @return The line, without a trailing newline, opened by K_MEASUREMENT_PREFIX.
         */
        [[nodiscard]] inline std::string format_measurement(const MeasurementLabel& label,
                                                            const RunStatistics& statistics)
        {
            char line[512];
            std::snprintf(line, sizeof(line),
                          "%s benchmark=%s case=%s unit=%s median=%.6g min=%.6g max=%.6g "
                          "runs=%zu build=%s workers=%zu size=%zu",
                          K_MEASUREMENT_PREFIX, label.benchmark, label.case_name, label.unit,
                          statistics.median, statistics.minimum, statistics.maximum,
                          statistics.run_count, build_type(), label.worker_count,
                          label.problem_size);
            return std::string(line);
        }

        /**
         * @brief Prints one measured case to stdout on a line of its own.
         */
        inline void print_measurement(const MeasurementLabel& label,
                                      const RunStatistics& statistics)
        {
            std::printf("%s\n", format_measurement(label, statistics).c_str());
            std::fflush(stdout);
        }

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
