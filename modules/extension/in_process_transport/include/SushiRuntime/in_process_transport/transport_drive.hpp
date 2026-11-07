/**************************************************************************/
/* transport_drive.hpp                                                    */
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
 * @file transport_drive.hpp
 * @brief Declares TransportDrive, the port that runs an in-process endpoint's pump rounds and times them.
 * @author Mustafa Garip
 * @see modules/extension/in_process_transport/README.md
 */

#pragma once

#include <cstdint>
#include <functional>

#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Runs the pump round of one endpoint when asked, and reads the time it is timed by.
         *
         * ThreadDrive runs the rounds on a thread it owns and reads the steady clock; ExecutorDrive
         * posts each round as a job of an Executor and reads the clock it was given. Both pass
         * TransportDriveContract.
         * @see modules/extension/in_process_transport/README.md
         */
        class TransportDrive
        {
            public:
                /** @brief One pass of the endpoint's pump. */
                using Round = std::function<void()>;

                /** @brief Stops the drive if it runs. */
                virtual ~TransportDrive() = default;

                /**
                 * @brief Binds the round and readies the drive to run it.
                 * @return Nothing, or an AllocationFailed error when a thread was refused.
                 * @pre Called once, before any other member but now().
                 */
                [[nodiscard]] virtual Result<void> start(Round round) = 0;

                /** @brief Asks for one run of the round; any thread, a round included, may call it. */
                virtual void post_round() = 0;

                /**
                 * @brief Asks for a run of the round once now() reaches a time.
                 *
                 * A drive whose clock someone else advances leaves the run to the next
                 * post_round(), which that owner makes.
                 */
                virtual void post_round_at(std::uint64_t at_ns) = 0;

                /** @brief Returns once no round runs and none will; a second call does nothing. */
                virtual void stop() noexcept = 0;

                /** @brief Returns the drive's time in nanoseconds; no reading is smaller than the one before. */
                [[nodiscard]] virtual std::uint64_t now() const noexcept = 0;

            protected:
                TransportDrive() = default;
                TransportDrive(const TransportDrive&) = default;
                TransportDrive& operator=(const TransportDrive&) = default;
                TransportDrive(TransportDrive&&) = default;
                TransportDrive& operator=(TransportDrive&&) = default;
        };
    } // namespace Distributed
} // namespace SushiRuntime
