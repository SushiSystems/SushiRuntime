/**************************************************************************/
/* heartbeat_drive.hpp                                                    */
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
 * @file heartbeat_drive.hpp
 * @brief Declares HeartbeatDrive, the port that calls a WorkloadCoordinator's beat on a heartbeat.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <chrono>
#include <functional>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /** @brief The time a drive waits between two beats. */
        inline constexpr std::chrono::milliseconds K_HEARTBEAT_INTERVAL{5};

        /**
         * @brief Calls a beat once on start and then once per K_HEARTBEAT_INTERVAL until stopped.
         * @see modules/engine/scheduler/README.md
         */
        class HeartbeatDrive
        {
            public:
                virtual ~HeartbeatDrive() = default;

                /**
                 * @brief Starts calling @p beat; does nothing while the drive runs.
                 * @param beat Called on the drive's thread or job; must outlive stop().
                 */
                virtual void start(std::function<void()> beat) = 0;

                /** @brief Stops the beats; no beat starts after it returns. Does nothing when stopped. */
                virtual void stop() = 0;

            protected:
                HeartbeatDrive() = default;
                HeartbeatDrive(const HeartbeatDrive&) = default;
                HeartbeatDrive& operator=(const HeartbeatDrive&) = default;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
