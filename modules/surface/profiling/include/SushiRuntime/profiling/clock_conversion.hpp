/**************************************************************************/
/* clock_conversion.hpp                                                   */
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
 * @file clock_conversion.hpp
 * @brief Declares the conversions from a device's event clock to the steady clock.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#pragma once

#include <cstdint>
#include <limits>

#include <SushiRuntime/report/clock_pairing.hpp>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        /**
         * @brief Returns @p event_ns, a reading of the device's event clock, on the steady clock.
         *
         * The pairing holds one reading of each clock taken beside each other, so the distance
         * from event_ns to the event reading is the distance from the result to the steady one.
         * A time before the steady clock's origin is 0.
         */
        [[nodiscard]] constexpr std::uint64_t to_steady_ns(const ClockPairing& pairing,
                                                           std::uint64_t event_ns) noexcept
        {
            if (event_ns >= pairing.event_ns)
                return pairing.steady_ns + (event_ns - pairing.event_ns);
            const std::uint64_t behind = pairing.event_ns - event_ns;
            return behind >= pairing.steady_ns ? 0U : pairing.steady_ns - behind;
        }

        /** @brief Returns @p uncertainty_ns as the 32 bits a span carries, saturating at the largest. */
        [[nodiscard]] constexpr std::uint32_t saturate_uncertainty(std::uint64_t uncertainty_ns) noexcept
        {
            constexpr std::uint64_t K_LARGEST = std::numeric_limits<std::uint32_t>::max();
            return static_cast<std::uint32_t>(uncertainty_ns > K_LARGEST ? K_LARGEST : uncertainty_ns);
        }
    } // namespace Profiling
} // namespace SushiRuntime
