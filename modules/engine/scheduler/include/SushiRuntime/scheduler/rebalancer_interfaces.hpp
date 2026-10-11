/**************************************************************************/
/* rebalancer_interfaces.hpp                                              */
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
 * @file rebalancer_interfaces.hpp
 * @brief Declares the telemetry, migration and strategy interfaces of the rebalancer.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

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
                 * @brief Returns how many tasks one worker has completed since it started.
                 * @param worker_id Worker index in [0, get_worker_count()).
                 * @see modules/engine/scheduler/README.md
                 */
                virtual size_t completed_task_count(int worker_id) const = 0;
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
                 * @brief Takes up to @p count tasks off one worker for the other workers to run.
                 * @param from_worker_id Source worker index.
                 * @param count          Maximum number of tasks to move.
                 */
                virtual void migrate_tasks(int from_worker_id, size_t count) = 0;
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
                 * @brief Inspects scheduler state and issues migration commands on the
                 *        coordinator's interval.
                 */
                virtual void evaluate_and_rebalance(const ITelemetryProvider& telemetry, ITaskMigrator& migrator) = 0;
        };
    }
}
