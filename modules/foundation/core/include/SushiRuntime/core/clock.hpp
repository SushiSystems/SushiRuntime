/**************************************************************************/
/* clock.hpp                                                              */
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
 * @file clock.hpp
 * @brief Declares Clock, the port every timed part of the runtime reads, and its two adapters.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Returns a monotonic time in nanoseconds; no reading is smaller than the one before.
     *
     * SteadyClock reads the machine's steady clock; ManualClock holds a time the caller sets, so
     * a test that times or waits advances it instead of sleeping. Both pass ClockContract.
     * @see modules/foundation/core/README.md
     */
    class Clock
    {
        public:
            /** @brief Releases nothing. */
            virtual ~Clock() = default;

            /** @brief Returns the time in nanoseconds since the clock's own epoch. */
            [[nodiscard]] virtual std::uint64_t now_ns() const noexcept = 0;

        protected:
            Clock() = default;
            Clock(const Clock&) = default;
            Clock& operator=(const Clock&) = default;
            Clock(Clock&&) = default;
            Clock& operator=(Clock&&) = default;
    };

    /**
     * @brief Reads std::chrono::steady_clock.
     */
    class SteadyClock final : public Clock
    {
        public:
            /** @brief Returns the steady clock's time since its epoch, in nanoseconds. */
            [[nodiscard]] std::uint64_t now_ns() const noexcept override
            {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
            }
    };

    /**
     * @brief Holds a time that moves only when the caller advances it.
     *
     * Safe to read from several threads while one thread advances it.
     */
    class ManualClock final : public Clock
    {
        public:
            /**
             * @brief Starts the clock at a time.
             * @param start_ns The first reading.
             */
            explicit ManualClock(std::uint64_t start_ns = 0) noexcept : now_(start_ns)
            {
            }

            /** @brief Returns the time the clock holds. */
            [[nodiscard]] std::uint64_t now_ns() const noexcept override
            {
                return now_.load(std::memory_order_acquire);
            }

            /**
             * @brief Moves the clock forward.
             * @param nanoseconds The step; 0 leaves the time as it is.
             */
            void advance(std::uint64_t nanoseconds) noexcept
            {
                now_.fetch_add(nanoseconds, std::memory_order_acq_rel);
            }

        private:
            /** @brief The time the clock holds, in nanoseconds. */
            std::atomic<std::uint64_t> now_;
    };
} // namespace SushiRuntime
