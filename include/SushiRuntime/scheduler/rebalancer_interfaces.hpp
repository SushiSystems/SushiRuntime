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
        // 1. İşçilerin anlık durumunu okuyan arayüz (Interface Segregation)
        class ITelemetryProvider 
        {
            public:
                virtual ~ITelemetryProvider() = default;
                virtual size_t get_worker_count() const = 0;
                virtual size_t get_queue_size(int worker_id) const = 0;
                // İşçinin son 1 milisaniyede bitirdiği iş sayısı (Hız/Throttling göstergesi)
                virtual size_t get_completion_rate(int worker_id) const = 0; 
        };

        // 2. İşçiler arasında zorla görev taşıyan arayüz
        class ITaskMigrator 
        {
            public:
                virtual ~ITaskMigrator() = default;
                // Slow_worker'dan fast_worker'a 'count' kadar görevi kilitlenmeden (lock-free) taşır
                virtual void migrate_tasks(int from_worker_id, int to_worker_id, size_t count) = 0;
        };

        // 3. Strateji Arayüzü (Open/Closed Principle)
        class IRebalanceStrategy 
        {
            public:
                virtual ~IRebalanceStrategy() = default;
                virtual void evaluate_and_rebalance(const ITelemetryProvider& telemetry, ITaskMigrator& migrator) = 0;
        };
    }
}