/**************************************************************************/
/* manual_heartbeat_clock.hpp                                             */
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
 * @file manual_heartbeat_clock.hpp
 * @brief Declares ManualHeartbeatClock, a HeartbeatClock that pauses until a caller advances it.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

#include <SushiRuntime/scheduler/heartbeat_clock.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Pauses each caller until advance() grants it a beat, and counts the pauses entered.
         *
         * The interval a caller asks for is ignored: one advance(1) ends one pause. release_all() ends
         * every pause now and later ones at once, so a thread parked in a pause can be joined.
         */
        class ManualHeartbeatClock final : public HeartbeatClock
        {
            public:
                /**
                 * @brief Counts the pause and blocks until a beat is granted or release_all() ran.
                 * @param interval Ignored.
                 */
                void pause(std::chrono::milliseconds interval) override
                {
                    static_cast<void>(interval);
                    std::unique_lock<std::mutex> lock(mutex_);
                    ++pauses_entered_;
                    changed_.notify_all();
                    changed_.wait(lock, [this]() { return released_all_ || beats_granted_ > beats_used_; });
                    if (!released_all_)
                        ++beats_used_;
                }

                /**
                 * @brief Lets @p beats pending or future pauses return.
                 * @param beats How many pauses may end.
                 */
                void advance(std::size_t beats)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    beats_granted_ += beats;
                    changed_.notify_all();
                }

                /** @brief Ends every pause now and makes later ones return at once. */
                void release_all()
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    released_all_ = true;
                    changed_.notify_all();
                }

                /**
                 * @brief Blocks until @p count pauses have been entered in all.
                 * @param count The total number of pauses to wait for.
                 */
                void wait_for_pauses(std::size_t count)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait(lock, [this, count]() { return pauses_entered_ >= count; });
                }

                /** @return How many pauses have been entered. */
                std::size_t pauses_entered() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return pauses_entered_;
                }

            private:
                mutable std::mutex mutex_;
                std::condition_variable changed_;
                std::size_t pauses_entered_ = 0;
                std::size_t beats_granted_ = 0;
                std::size_t beats_used_ = 0;
                bool released_all_ = false;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
