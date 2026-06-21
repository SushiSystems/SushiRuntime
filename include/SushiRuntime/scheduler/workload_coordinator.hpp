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
        /**
         * @brief Background monitor that drives an IRebalanceStrategy on a fixed heartbeat.
         *
         * Runs a dedicated low-overhead coordinator thread that calls
         * IRebalanceStrategy::evaluate_and_rebalance every 5 ms. The strategy
         * inspects live telemetry via ITelemetryProvider and issues task migrations
         * via ITaskMigrator. The strategy can be swapped at runtime while the
         * coordinator is running.
         */
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
                    while (running_.load(std::memory_order_relaxed))
                    {
                        strategy_->evaluate_and_rebalance(telemetry_, migrator_);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }

            public:
                /**
                 * @brief Constructs a coordinator but does not start the thread.
                 * @param t        Telemetry source (must outlive this object).
                 * @param m        Migration actuator (must outlive this object).
                 * @param s        Initial rebalance strategy.
                 */
                WorkloadCoordinator(const ITelemetryProvider& t, ITaskMigrator& m, std::unique_ptr<IRebalanceStrategy> s)
                    : telemetry_(t), migrator_(m), strategy_(std::move(s)) {}

                /** @brief Calls stop() if the coordinator thread is still running. */
                ~WorkloadCoordinator() { stop(); }

                /**
                 * @brief Starts the coordinator thread. No-op if already running.
                 */
                void start()
                {
                    if (!running_.exchange(true))
                        coordinator_thread_ = std::thread(&WorkloadCoordinator::monitor_loop, this);
                }

                /**
                 * @brief Signals the coordinator to stop and joins its thread. No-op if not running.
                 */
                void stop()
                {
                    if (running_.exchange(false))
                        if (coordinator_thread_.joinable())
                            coordinator_thread_.join();
                }

                /**
                 * @brief Replaces the active rebalance strategy.
                 *
                 * Safe to call while the coordinator is running; the next heartbeat
                 * fires the new strategy.
                 *
                 * @param new_strategy Replacement strategy (takes ownership).
                 */
                void set_strategy(std::unique_ptr<IRebalanceStrategy> new_strategy)
                {
                    strategy_ = std::move(new_strategy);
                }
        };
    }
}
