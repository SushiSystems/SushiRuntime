/**************************************************************************/
/* workload_coordinator.hpp                                               */
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
 * @file workload_coordinator.hpp
 * @brief Declares WorkloadCoordinator, which runs a rebalance strategy on a heartbeat.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Drives an RebalanceStrategy from a background thread on a 5 ms heartbeat.
         * @see modules/engine/scheduler/README.md
         */
        class WorkloadCoordinator
        {
            private:
                const TelemetryProvider& telemetry_;
                TaskMigrator& migrator_;

                /**
                 * @brief Guards strategy_ against set_strategy() destroying it mid-call.
                 * @see modules/engine/scheduler/README.md
                 */
                mutable std::mutex strategy_mtx_;
                std::unique_ptr<RebalanceStrategy> strategy_;

                std::atomic<bool> running_{false};
                std::thread coordinator_thread_;

                void monitor_loop()
                {
                    while (running_.load(std::memory_order_relaxed))
                    {
                        {
                            std::lock_guard<std::mutex> lk(strategy_mtx_);
                            if (strategy_)
                                strategy_->evaluate_and_rebalance(telemetry_, migrator_);
                        }
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
                WorkloadCoordinator(const TelemetryProvider& t, TaskMigrator& m, std::unique_ptr<RebalanceStrategy> s)
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
                void set_strategy(std::unique_ptr<RebalanceStrategy> new_strategy)
                {
                    std::unique_ptr<RebalanceStrategy> old;
                    {
                        std::lock_guard<std::mutex> lk(strategy_mtx_);
                        old = std::move(strategy_);
                        strategy_ = std::move(new_strategy);
                    }
                    // old is destroyed here, outside the lock; monitor_loop cannot be using it.
                }
        };
    }
}
