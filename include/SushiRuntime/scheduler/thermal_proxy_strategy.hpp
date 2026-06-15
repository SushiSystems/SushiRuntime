/**************************************************************************/
/* thermal_proxy_strategy.hpp                                             */
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
#include "rebalancer_interfaces.hpp"
#include <SushiRuntime/core/logger.hpp>
#include <algorithm>
#include <vector>

namespace SushiRuntime 
{
    namespace Scheduler 
    {
        class ThermalProxyStrategy : public IRebalanceStrategy 
        {
        private:
            static constexpr size_t IMBALANCE_THRESHOLD = 20; // 20 task fark varsa tetikle
            static constexpr size_t THROTTLING_RATE = 5;      // Delta (Hız) 5'ten küçükse boğuluyor demektir
            
            std::vector<size_t> last_completed_counts_;

        public:
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
                    size_t total_completed = telemetry.get_completion_rate(i); // Aslında total
                    
                    // Delta: Son 5ms'de kaç task bitirdi? (Gerçek işlemci hızı)
                    size_t current_rate = total_completed - last_completed_counts_[i];
                    last_completed_counts_[i] = total_completed;

                    // Eğer bir çekirdeğin kuyruğu şişiyor ama hızı yerlerde sürünüyorsa (Isınmış / Throttled)
                    if (q_size > max_queue && current_rate <= THROTTLING_RATE && q_size > IMBALANCE_THRESHOLD) 
                    {
                        max_queue = q_size;
                        bottleneck_worker = i;
                    }

                    // Eğer bir çekirdek bomboşsa (Soğuk)
                    if (q_size < min_queue) 
                    {
                        min_queue = q_size;
                        idle_worker = i;
                    }
                }

                if (bottleneck_worker != -1 && idle_worker != -1 && bottleneck_worker != idle_worker) 
                {
                    // Boğulan çekirdeğin yükünün yarısını çal (En az 10 task)
                    size_t tasks_to_move = std::max(static_cast<size_t>(10), (max_queue - min_queue) / 2);
                    migrator.migrate_tasks(bottleneck_worker, idle_worker, tasks_to_move);
                    
                    SR_LOG_DEBUG("Rebalancer: Rescued {} tasks from struggling Core {} (Rate: {}) -> Idle Core {}", 
                                 tasks_to_move, bottleneck_worker, (max_queue - last_completed_counts_[bottleneck_worker]), idle_worker);
                }
            }
        };
    }
}