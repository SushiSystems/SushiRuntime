/**************************************************************************/
/* rebalancer_interfaces.hpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once
#include <vector>
#include <cstdint>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Read-only view of live scheduler state for rebalance decisions.
         *
         * Implemented by TaskScheduler; injected into IRebalanceStrategy so the
         * strategy can inspect queue depths and completion rates without coupling
         * to the scheduler's internals.
         */
        class ITelemetryProvider
        {
            public:
                virtual ~ITelemetryProvider() = default;

                /** @return Total number of active worker threads. */
                virtual size_t get_worker_count() const = 0;

                /**
                 * @brief Pending task count for one worker.
                 * @param worker_id Worker index in [0, get_worker_count()).
                 * @return Number of tasks currently queued for that worker.
                 */
                virtual size_t get_queue_size(int worker_id) const = 0;

                /**
                 * @brief Cumulative completed-task count for one worker.
                 *
                 * This is a running total, not a per-interval rate; callers
                 * diff two snapshots to compute a rate over an interval.
                 *
                 * @param worker_id Worker index in [0, get_worker_count()).
                 * @return Total tasks completed by that worker since scheduler start.
                 */
                virtual size_t get_completion_rate(int worker_id) const = 0;
        };

        /**
         * @brief Forcibly moves tasks between workers to balance load.
         *
         * Implemented by TaskScheduler; injected into IRebalanceStrategy so the
         * strategy can trigger migration without touching scheduler internals.
         */
        class ITaskMigrator
        {
            public:
                virtual ~ITaskMigrator() = default;

                /**
                 * @brief Moves up to @p count tasks from one worker to another.
                 * @param from_worker_id Source worker index.
                 * @param to_worker_id   Destination worker index.
                 * @param count          Maximum number of tasks to move.
                 */
                virtual void migrate_tasks(int from_worker_id, int to_worker_id, size_t count) = 0;
        };

        /**
         * @brief Policy interface for deciding when and how to rebalance workers.
         *
         * Injected into WorkloadCoordinator. Each strategy sees the same
         * telemetry/migrator pair and implements its own load-detection logic.
         * Swap strategies at runtime via WorkloadCoordinator::set_strategy().
         */
        class IRebalanceStrategy
        {
            public:
                virtual ~IRebalanceStrategy() = default;

                /**
                 * @brief Inspects scheduler state and issues migration commands.
                 *
                 * Called on a fixed interval by WorkloadCoordinator. Implementations
                 * read @p telemetry to detect imbalance and call
                 * @p migrator to move tasks between workers.
                 *
                 * @param telemetry Live scheduler counters (read-only).
                 * @param migrator  Migration actuator to move tasks.
                 */
                virtual void evaluate_and_rebalance(const ITelemetryProvider& telemetry, ITaskMigrator& migrator) = 0;
        };
    }
}
