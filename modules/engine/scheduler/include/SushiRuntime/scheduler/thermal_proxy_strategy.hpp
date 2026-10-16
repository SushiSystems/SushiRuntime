/**************************************************************************/
/* thermal_proxy_strategy.hpp                                             */
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
 * @file thermal_proxy_strategy.hpp
 * @brief Declares ThermalProxyStrategy, a rebalance strategy for throttled workers.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <algorithm>
#include <vector>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Detects a throttled worker and asks the migrator to take tasks off its queue.
         * @see modules/engine/scheduler/README.md
         */
        class ThermalProxyStrategy : public RebalanceStrategy
        {
            private:
                /** @brief Queue-depth imbalance (tasks) required to trigger migration. */
                static constexpr size_t K_IMBALANCE_THRESHOLD = 20;
                /** @brief Maximum completion-rate delta (tasks/interval) treated as throttled. */
                static constexpr size_t K_THROTTLING_RATE = 5;

                std::vector<size_t> last_completed_counts_;

            public:
                /**
                 * @brief Scans all workers for a throttled bottleneck and takes tasks off its queue.
                 *
                 * Calls migrate_tasks(bottleneck, count), where count is half the gap between the
                 * bottleneck's queue and the shortest one, and at least 10. The call names no
                 * destination; the worker with the shortest queue appears only in the log line.
                 */
                void evaluate_and_rebalance(const TelemetryProvider& telemetry, TaskMigrator& migrator) override
                {
                    size_t num_workers = telemetry.worker_count();
                    if (num_workers < 2) return;

                    if (last_completed_counts_.size() != num_workers)
                        last_completed_counts_.resize(num_workers, 0);

                    int bottleneck_worker = -1;
                    int idle_worker = -1;
                    size_t max_queue = 0;
                    size_t min_queue = SIZE_MAX;
                    size_t bottleneck_rate = 0;

                    for (size_t i = 0; i < num_workers; ++i)
                    {
                        size_t q_size = telemetry.queue_size(i);
                        size_t total_completed = telemetry.completed_task_count(i);

                        size_t current_rate = total_completed - last_completed_counts_[i];
                        last_completed_counts_[i] = total_completed;

                        if (q_size > max_queue && current_rate <= K_THROTTLING_RATE && q_size > K_IMBALANCE_THRESHOLD)
                        {
                            max_queue = q_size;
                            bottleneck_worker = i;
                            bottleneck_rate = current_rate;
                        }

                        if (q_size < min_queue)
                        {
                            min_queue = q_size;
                            idle_worker = i;
                        }
                    }

                    if (bottleneck_worker != -1 && idle_worker != -1 && bottleneck_worker != idle_worker)
                    {
                        size_t tasks_to_move = std::max(static_cast<size_t>(10), (max_queue - min_queue) / 2);
                        migrator.migrate_tasks(bottleneck_worker, tasks_to_move);

                        SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "Rebalancer: took up to {} tasks off worker {} (queue depth "
                                     "{}, {} completed since the last check); the idlest worker "
                                     "is {} (queue depth {}).",
                                     tasks_to_move, bottleneck_worker, max_queue, bottleneck_rate,
                                     idle_worker, min_queue);
                    }
                }
        };
    }
}
