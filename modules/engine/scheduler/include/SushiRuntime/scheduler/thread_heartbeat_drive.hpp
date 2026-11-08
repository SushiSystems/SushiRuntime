/**************************************************************************/
/* thread_heartbeat_drive.hpp                                             */
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
 * @file thread_heartbeat_drive.hpp
 * @brief Declares ThreadHeartbeatDrive, a HeartbeatDrive that beats on a thread of its own.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <utility>

#include <SushiRuntime/scheduler/heartbeat_clock.hpp>
#include <SushiRuntime/scheduler/heartbeat_drive.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Beats on a thread it starts, pausing K_HEARTBEAT_INTERVAL on a HeartbeatClock
         *        between two beats.
         */
        class ThreadHeartbeatDrive final : public HeartbeatDrive
        {
            public:
                /** @brief Builds a drive that pauses on the system clock. */
                ThreadHeartbeatDrive() noexcept : clock_(&system_clock_)
                {

                }

                /**
                 * @brief Builds a drive that pauses on @p clock.
                 * @param clock Must outlive the drive.
                 */
                explicit ThreadHeartbeatDrive(HeartbeatClock& clock) noexcept : clock_(&clock)
                {

                }

                /** @brief Stops the thread. */
                ~ThreadHeartbeatDrive() override { stop(); }

                ThreadHeartbeatDrive(const ThreadHeartbeatDrive&) = delete;
                ThreadHeartbeatDrive& operator=(const ThreadHeartbeatDrive&) = delete;
                ThreadHeartbeatDrive(ThreadHeartbeatDrive&&) = delete;
                ThreadHeartbeatDrive& operator=(ThreadHeartbeatDrive&&) = delete;

                /** @brief Starts the thread, which beats and then pauses until stopped. */
                void start(std::function<void()> beat) override
                {
                    if (running_.exchange(true))
                        return;
                    thread_ = std::thread([this, beat = std::move(beat)]()
                    {
                        while (running_.load(std::memory_order_relaxed))
                        {
                            beat();
                            clock_->pause(K_HEARTBEAT_INTERVAL);
                        }
                    });
                }

                /** @brief Signals the thread to stop and joins it. */
                void stop() override
                {
                    if (running_.exchange(false) && thread_.joinable())
                        thread_.join();
                }

            private:
                /// @brief The clock a drive built without one pauses on.
                SystemHeartbeatClock system_clock_;
                /// @brief The clock the thread pauses on.
                HeartbeatClock* clock_;
                /// @brief True from start() until stop().
                std::atomic<bool> running_{false};
                /// @brief The beating thread.
                std::thread thread_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
