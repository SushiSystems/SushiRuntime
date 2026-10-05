/****************************************************************/
/* thermal_proxy_strategy.hpp                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once
#include "rebalancer_interfaces.hpp"
#include <SushiRuntime/core/logger.hpp>
#include <algorithm>
#include <vector>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Rebalance strategy that detects throttled (hot) workers and offloads them.
         *
         * Identifies the worker whose queue is growing fastest but completing
         * fewest tasks (a thermal or load-spike signature) and moves half of its
         * backlog to the idlest worker. Fires only when the queue imbalance
         * exceeds IMBALANCE_THRESHOLD and the victim's completion rate is at or
         * below THROTTLING_RATE.
         */
        class ThermalProxyStrategy : public IRebalanceStrategy
        {
            private:
                /** @brief Queue-depth imbalance (tasks) required to trigger migration. */
                static constexpr size_t IMBALANCE_THRESHOLD = 20;
                /** @brief Maximum completion-rate delta (tasks/interval) treated as throttled. */
                static constexpr size_t THROTTLING_RATE = 5;

                std::vector<size_t> last_completed_counts_;

            public:
                /**
                 * @brief Scans all workers for a throttled bottleneck and migrates tasks.
                 *
                 * Compares each worker's queue depth and per-interval completion rate.
                 * When a bottleneck (high queue, low rate) and an idle worker (lowest
                 * queue) are found, moves half the imbalance to the idle worker.
                 *
                 * @param telemetry Live scheduler counters.
                 * @param migrator  Migration actuator to move tasks.
                 */
                void evaluate_and_rebalance(const ITelemetryProvider& telemetry, ITaskMigrator& migrator) override
                {
                    size_t num_workers = telemetry.get_worker_count();
                    if (num_workers < 2) return;

                    if (last_completed_counts_.size() != num_workers)
                        last_completed_counts_.resize(num_workers, 0);

                    int bottleneck_worker = -1;
                    int idle_worker = -1;
                    size_t max_queue = 0;
                    size_t min_queue = SIZE_MAX;

                    for (size_t i = 0; i < num_workers; ++i)
                    {
                        size_t q_size = telemetry.get_queue_size(i);
                        size_t total_completed = telemetry.get_completion_rate(i);

                        size_t current_rate = total_completed - last_completed_counts_[i];
                        last_completed_counts_[i] = total_completed;

                        if (q_size > max_queue && current_rate <= THROTTLING_RATE && q_size > IMBALANCE_THRESHOLD)
                        {
                            max_queue = q_size;
                            bottleneck_worker = i;
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
                        migrator.migrate_tasks(bottleneck_worker, idle_worker, tasks_to_move);

                        SR_LOG_DEBUG("Rebalancer: Rescued {} tasks from struggling Core {} (Rate: {}) -> Idle Core {}",
                                     tasks_to_move, bottleneck_worker, (max_queue - last_completed_counts_[bottleneck_worker]), idle_worker);
                    }
                }
        };
    }
}
