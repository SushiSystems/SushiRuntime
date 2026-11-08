/**************************************************************************/
/* executor_heartbeat_drive.hpp                                           */
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
 * @file executor_heartbeat_drive.hpp
 * @brief Declares ExecutorHeartbeatDrive, a HeartbeatDrive that beats as a job on an Executor.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/executor.hpp>
#include <SushiRuntime/scheduler/heartbeat_drive.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Beats as one executor job that beats when the clock reaches the next beat and
         *        posts itself again; starts no thread.
         */
        class ExecutorHeartbeatDrive final : public HeartbeatDrive
        {
            public:
                /**
                 * @brief Builds a drive over an executor and a clock; posts nothing.
                 * @param executor Runs the job; must outlive the drive and run no job after it.
                 * @param clock The job reads the time of the next beat from it; must outlive the drive.
                 */
                ExecutorHeartbeatDrive(Executor& executor, const Clock& clock) noexcept
                    : executor_(executor), clock_(clock)
                {

                }

                /** @brief Posts the job, which beats on its first run. */
                void start(std::function<void()> beat) override
                {
                    if (running_)
                        return;
                    running_ = true;
                    beat_ = std::move(beat);
                    next_beat_ns_ = clock_.now_ns();
                    if (!job_posted_)
                        post();
                }

                /** @brief Makes a posted job return without beating. */
                void stop() override
                {
                    running_ = false;
                }

            private:
                /** @brief Posts the job, which beats when due and posts itself again while running. */
                void post()
                {
                    job_posted_ = true;
                    executor_.post([this]()
                    {
                        job_posted_ = false;
                        if (!running_)
                            return;
                        const std::uint64_t now = clock_.now_ns();
                        if (now >= next_beat_ns_)
                        {
                            beat_();
                            next_beat_ns_ = now + static_cast<std::uint64_t>(
                                std::chrono::nanoseconds(K_HEARTBEAT_INTERVAL).count());
                        }
                        post();
                    });
                }

                /// @brief Runs the job.
                Executor& executor_;
                /// @brief The clock the job reads.
                const Clock& clock_;
                /// @brief The beat start() was given.
                std::function<void()> beat_;
                /// @brief The clock time of the next beat.
                std::uint64_t next_beat_ns_ = 0;
                /// @brief True from start() until stop().
                bool running_ = false;
                /// @brief True while a job is posted and not yet run.
                bool job_posted_ = false;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
