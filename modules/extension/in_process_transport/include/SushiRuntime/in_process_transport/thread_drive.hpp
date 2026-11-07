/**************************************************************************/
/* thread_drive.hpp                                                       */
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
 * @file thread_drive.hpp
 * @brief Declares ThreadDrive, the TransportDrive that runs pump rounds on a thread it owns.
 * @author Mustafa Garip
 * @see modules/extension/in_process_transport/README.md
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/in_process_transport/transport_drive.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Runs the round on one thread, started by start() and joined by stop(), timed by
         *        the steady clock.
         * @see modules/extension/in_process_transport/README.md
         */
        class SUSHIRUNTIME_API ThreadDrive final : public TransportDrive
        {
            public:
                ThreadDrive() = default;
                ThreadDrive(const ThreadDrive&) = delete;
                ThreadDrive& operator=(const ThreadDrive&) = delete;
                ThreadDrive(ThreadDrive&&) = delete;
                ThreadDrive& operator=(ThreadDrive&&) = delete;

                /** @brief Joins the thread. */
                ~ThreadDrive() override;

                /** @brief Starts the thread. */
                [[nodiscard]] Result<void> start(Round round) override;

                /** @brief Wakes the thread for one round. */
                void post_round() override;

                /** @brief Wakes the thread for one round when the steady clock reaches at_ns. */
                void post_round_at(std::uint64_t at_ns) override;

                /** @brief Joins the thread after the round in progress; a call from a round joins nothing. */
                void stop() noexcept override;

                /** @brief Reads the steady clock. */
                [[nodiscard]] std::uint64_t now() const noexcept override;

            private:
                /** @brief Runs rounds until stop(). */
                void run();

                /** @brief The clock every reading and every timed wake uses. */
                SteadyClock clock_;

                /** @brief The round start() bound. */
                Round round_;

                /** @brief Guards the three fields below. */
                std::mutex mutex_;

                /** @brief Wakes the thread. */
                std::condition_variable wake_;

                /** @brief Set by post_round() until a round starts. */
                bool requested_ = false;

                /** @brief Set by stop(). */
                bool stopping_ = false;

                /** @brief The earliest time a post_round_at() asked for. */
                std::optional<std::uint64_t> wake_at_;

                /** @brief The thread that runs the rounds. */
                std::thread thread_;
        };
    } // namespace Distributed
} // namespace SushiRuntime
