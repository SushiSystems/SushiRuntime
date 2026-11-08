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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file workload_coordinator.hpp
 * @brief Declares WorkloadCoordinator, which runs a rebalance strategy on the beats of a drive.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <chrono>
#include <memory>
#include <mutex>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/scheduler/executor_heartbeat_drive.hpp>
#include <SushiRuntime/scheduler/heartbeat_clock.hpp>
#include <SushiRuntime/scheduler/heartbeat_drive.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/thread_heartbeat_drive.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Runs a RebalanceStrategy once per beat of the HeartbeatDrive it is given.
         *
         * The drive decides where the beats run: ThreadHeartbeatDrive on a thread of its own,
         * ExecutorHeartbeatDrive as a job on an Executor.
         * @see modules/engine/scheduler/README.md
         */
        class WorkloadCoordinator
        {
            public:
                /** @brief The time the drive waits between two beats. */
                static constexpr std::chrono::milliseconds K_HEARTBEAT_INTERVAL =
                    Internal::Scheduler::K_HEARTBEAT_INTERVAL;

                /**
                 * @brief Builds a coordinator whose beats run on @p drive; starts nothing.
                 * @param t Telemetry source; must outlive this object.
                 * @param m Migration actuator; must outlive this object.
                 * @param drive Must not be null.
                 */
                WorkloadCoordinator(const TelemetryProvider& t, TaskMigrator& m,
                                    std::unique_ptr<RebalanceStrategy> s,
                                    std::unique_ptr<HeartbeatDrive> drive)
                    : telemetry_(t), migrator_(m), strategy_(std::move(s)), drive_(std::move(drive))
                {

                }

                /** @brief Builds a coordinator that beats on a thread pausing on the system clock. */
                WorkloadCoordinator(const TelemetryProvider& t, TaskMigrator& m,
                                    std::unique_ptr<RebalanceStrategy> s)
                    : WorkloadCoordinator(t, m, std::move(s), std::make_unique<ThreadHeartbeatDrive>())
                {

                }

                /**
                 * @brief Builds a coordinator that beats on a thread pausing on @p clock.
                 * @param clock Must outlive this object.
                 */
                WorkloadCoordinator(const TelemetryProvider& t, TaskMigrator& m,
                                    std::unique_ptr<RebalanceStrategy> s, HeartbeatClock& clock)
                    : WorkloadCoordinator(t, m, std::move(s),
                                          std::make_unique<ThreadHeartbeatDrive>(clock))
                {

                }

                /**
                 * @brief Builds a coordinator whose heartbeat is a job on @p executor; starts nothing.
                 * @param executor Must outlive this object and run no job after it.
                 * @param clock The job reads the time of the next beat from it; must outlive this object.
                 */
                WorkloadCoordinator(const TelemetryProvider& t, TaskMigrator& m,
                                    std::unique_ptr<RebalanceStrategy> s, Executor& executor,
                                    const Clock& clock)
                    : WorkloadCoordinator(t, m, std::move(s),
                                          std::make_unique<ExecutorHeartbeatDrive>(executor, clock))
                {

                }

                /** @brief Stops the drive. */
                ~WorkloadCoordinator() { stop(); }

                WorkloadCoordinator(const WorkloadCoordinator&) = delete;
                WorkloadCoordinator& operator=(const WorkloadCoordinator&) = delete;
                WorkloadCoordinator(WorkloadCoordinator&&) = delete;
                WorkloadCoordinator& operator=(WorkloadCoordinator&&) = delete;

                /**
                 * @brief Evaluates the active strategy once, as one heartbeat does.
                 *
                 * Safe to call from any thread; it does nothing when no strategy is set.
                 */
                void beat()
                {
                    std::lock_guard<std::mutex> lk(strategy_mtx_);
                    if (strategy_)
                        strategy_->evaluate_and_rebalance(telemetry_, migrator_);
                }

                /** @brief Starts the drive's beats. No-op if already running. */
                void start()
                {
                    drive_->start([this]() { beat(); });
                }

                /** @brief Stops the drive's beats. No-op if not running. */
                void stop()
                {
                    drive_->stop();
                }

                /**
                 * @brief Replaces the active rebalance strategy; the next beat runs the new one.
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
                    // old is destroyed here, outside the lock; no beat can be using it.
                }

            private:
                /// @brief Telemetry source.
                const TelemetryProvider& telemetry_;
                /// @brief Migration actuator.
                TaskMigrator& migrator_;
                /// @brief Guards strategy_ against set_strategy() destroying it mid-call.
                mutable std::mutex strategy_mtx_;
                /// @brief The active strategy; null runs nothing.
                std::unique_ptr<RebalanceStrategy> strategy_;
                /// @brief Runs the beats; declared last, so it stops before the members above die.
                std::unique_ptr<HeartbeatDrive> drive_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
