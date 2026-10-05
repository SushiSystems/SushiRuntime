/****************************************************************/
/* workload_coordinator.hpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once
#include "rebalancer_interfaces.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

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

                // strategy_mtx_ guards strategy_ against a concurrent set_strategy()
                // destroying the object the monitor thread is mid-call on. Held
                // only across the (fast) evaluate_and_rebalance call and the swap,
                // never across the 5 ms sleep.
                mutable std::mutex strategy_mtx_;
                std::unique_ptr<IRebalanceStrategy> strategy_;

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
                    std::unique_ptr<IRebalanceStrategy> old;
                    {
                        std::lock_guard<std::mutex> lk(strategy_mtx_);
                        old = std::move(strategy_);
                        strategy_ = std::move(new_strategy);
                    }
                    // old is destroyed here, outside the lock and guaranteed not to
                    // be in use by monitor_loop (which holds the same mutex around
                    // its evaluate_and_rebalance call).
                }
        };
    }
}
