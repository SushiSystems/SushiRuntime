/**************************************************************************/
/* in_memory_telemetry.hpp                                                */
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
 * @file in_memory_telemetry.hpp
 * @brief Declares InMemoryTelemetry, a TelemetryProvider that answers from values a caller sets.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Answers telemetry queries from per-worker queue depths and completion totals held in memory.
         *
         * Worker i has queue depth queue_sizes[i] and completion total completed_counts[i]; the worker
         * count is the length of queue_sizes. Both vectors have the same length.
         */
        class InMemoryTelemetry final : public TelemetryProvider
        {
            public:
                /**
                 * @brief Holds the depths and totals the queries answer with.
                 * @param queue_sizes      Pending task count of each worker.
                 * @param completed_counts Completed task total of each worker.
                 */
                InMemoryTelemetry(std::vector<size_t> queue_sizes, std::vector<size_t> completed_counts)
                    : queue_sizes_(std::move(queue_sizes)), completed_counts_(std::move(completed_counts))
                {
                }

                /** @return The number of workers the telemetry holds. */
                size_t worker_count() const override
                {
                    return queue_sizes_.size();
                }

                /**
                 * @brief Returns the held queue depth of one worker.
                 * @param worker_id Worker index in [0, worker_count()).
                 */
                size_t queue_size(int worker_id) const override
                {
                    return queue_sizes_.at(static_cast<size_t>(worker_id));
                }

                /**
                 * @brief Returns the held completion total of one worker.
                 * @param worker_id Worker index in [0, worker_count()).
                 */
                size_t completed_task_count(int worker_id) const override
                {
                    return completed_counts_.at(static_cast<size_t>(worker_id));
                }

            private:
                std::vector<size_t> queue_sizes_;
                std::vector<size_t> completed_counts_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
