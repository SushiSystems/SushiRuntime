/**************************************************************************/
/* workload_coordinator.hpp                                               */
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
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace SushiRuntime 
{
    namespace Scheduler 
    {
        class WorkloadCoordinator 
        {
            private:
                const ITelemetryProvider& telemetry_;
                ITaskMigrator& migrator_;
                std::unique_ptr<IRebalanceStrategy> strategy_;
                
                std::atomic<bool> running_{false};
                std::thread coordinator_thread_;

                void monitor_loop() 
                {
                    // Koordinatör OS Jitter yaratmasın diye düşük öncelikli çalışabilir veya izole edilebilir.
                    while (running_.load(std::memory_order_relaxed)) 
                    {
                        // Stratejiyi çalıştır ve gerekirse müdahale et
                        strategy_->evaluate_and_rebalance(telemetry_, migrator_);
                        
                        // İşlemciyi boğmamak için bekle (Her 5 milisaniyede bir kalp atışı)
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }

            public:
                WorkloadCoordinator(const ITelemetryProvider& t, ITaskMigrator& m, std::unique_ptr<IRebalanceStrategy> s)
                    : telemetry_(t), migrator_(m), strategy_(std::move(s)) {}

                ~WorkloadCoordinator() { stop(); }

                void start() 
                {
                    if (!running_.exchange(true)) 
                        coordinator_thread_ = std::thread(&WorkloadCoordinator::monitor_loop, this);
                }

                void stop() 
                {
                    if (running_.exchange(false)) 
                        if (coordinator_thread_.joinable()) 
                            coordinator_thread_.join();
                }

                // OCP: Çalışma zamanında strateji değiştirilebilir!
                void set_strategy(std::unique_ptr<IRebalanceStrategy> new_strategy) 
                {
                    strategy_ = std::move(new_strategy);
                }
        };
    }
}