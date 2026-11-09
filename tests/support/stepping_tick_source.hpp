/**************************************************************************/
/* stepping_tick_source.hpp                                               */
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
 * @file stepping_tick_source.hpp
 * @brief Declares SteppingTickSource, a SushiProfiler tick source that moves by one on every reading.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <SushiProfiler/clock/calibration.hpp>
#include <SushiProfiler/clock/tick_source.hpp>

namespace TestSupport
{
    /**
     * @brief Stamps with a count that grows by one on every reading.
     *
     * A zone that opens after another and closes before it ends gets a begin and an end strictly inside
     * the outer one, so a record built from the stamps nests the same on every run. SushiProfiler's
     * ManualTickSource moves only when a test advances it, which leaves nested zones with equal stamps.
     */
    class SteppingTickSource final : public SushiProfiler::TickSource
    {
        public:
            /** @brief Returns the next stamp. */
            [[nodiscard]] std::uint64_t now() const noexcept override
            {
                return count_.fetch_add(1U, std::memory_order_relaxed) + 1U;
            }

            /** @brief Returns the next stamp as both the tick and the steady reading. */
            [[nodiscard]] SushiProfiler::CalibrationPoint sample() const noexcept override
            {
                const std::uint64_t stamp = now();
                return SushiProfiler::CalibrationPoint{stamp, stamp};
            }

            /** @brief Returns false: the stamps are nanoseconds. */
            [[nodiscard]] bool ticks_are_tsc() const noexcept override
            {
                return false;
            }

        private:
            mutable std::atomic<std::uint64_t> count_{0U}; /**< The last stamp given. */
    };
} // namespace TestSupport
