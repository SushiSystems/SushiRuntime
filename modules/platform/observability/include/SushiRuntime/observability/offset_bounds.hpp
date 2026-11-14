/**************************************************************************/
/* offset_bounds.hpp                                                      */
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
 * @file offset_bounds.hpp
 * @brief Declares OffsetBounds and the functions that bound a device's event clock from its dispatches.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include <SushiRuntime/report/clock_pairing.hpp>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /** @brief Holds what the host saw of one dispatch, on the steady clock and on its device's event clock. */
        struct DispatchWindow
        {
            std::uint64_t submit_steady_ns = 0; /**< Steady reading taken before the submit call began. */
            std::uint64_t start_ns = 0;         /**< command_start, on the event clock. */
            std::uint64_t end_ns = 0;           /**< command_end, on the event clock. */
            std::uint64_t reap_steady_ns = 0;   /**< Steady reading taken after the host saw the dispatch complete. */
        };

        /**
         * @brief Bounds the offset a device's event clock needs to land on the steady clock.
         *
         * The offset is a steady reading minus the event reading of the same instant.
         * @see modules/platform/observability/README.md
         */
        struct OffsetBounds
        {
            std::int64_t low_ns = std::numeric_limits<std::int64_t>::min();  /**< The offset is at least this. */
            std::int64_t high_ns = std::numeric_limits<std::int64_t>::max(); /**< The offset is at most this. */
        };

        /**
         * @brief Returns @p a minus @p b as a signed count, in two's complement.
         * @pre The true difference lies within the range of std::int64_t.
         */
        [[nodiscard]] constexpr std::int64_t signed_difference(std::uint64_t a, std::uint64_t b) noexcept
        {
            return static_cast<std::int64_t>(a - b);
        }

        /**
         * @brief Returns the bounds one dispatch places on its device's offset.
         *
         * The kernel cannot start before the host began to submit it, and it ended before the host saw it
         * complete, so the offset lies between submit minus start and reap minus end.
         */
        [[nodiscard]] constexpr OffsetBounds bounds_of(const DispatchWindow& window) noexcept
        {
            return OffsetBounds{signed_difference(window.submit_steady_ns, window.start_ns),
                                signed_difference(window.reap_steady_ns, window.end_ns)};
        }

        /** @brief Returns the bounds both @p a and @p b allow: the larger low and the smaller high. */
        [[nodiscard]] constexpr OffsetBounds narrowed(const OffsetBounds& a, const OffsetBounds& b) noexcept
        {
            return OffsetBounds{std::max(a.low_ns, b.low_ns), std::min(a.high_ns, b.high_ns)};
        }

        /**
         * @brief The largest drift between a device's event clock and the steady clock a pairing allows for, in ppm.
         * @see modules/platform/observability/README.md
         */
        inline constexpr std::uint64_t K_MAX_CLOCK_DRIFT_PPM = 100;

        /** @brief Returns how far the offset can move over @p span_ns at K_MAX_CLOCK_DRIFT_PPM, rounded up. */
        [[nodiscard]] constexpr std::uint64_t drift_over(std::uint64_t span_ns) noexcept
        {
            return (span_ns * K_MAX_CLOCK_DRIFT_PPM + 999'999U) / 1'000'000U;
        }

        /**
         * @brief Returns the pairing at the midpoint of @p bounds, half their width plus @p drift_ns its uncertainty.
         * @param bounds Finite bounds from at least one dispatch.
         * @param reference_event_ns An event-clock reading of a dispatch the bounds came from.
         * @param drift_ns The most the offset can move across the dispatches, drift_over() their steady span.
         * @pre @p reference_event_ns moved by the midpoint offset is not before the steady clock's origin.
         */
        [[nodiscard]] constexpr ClockPairing pairing_from(const OffsetBounds& bounds, std::uint64_t reference_event_ns,
                                                          std::uint64_t drift_ns) noexcept
        {
            const std::uint64_t low = static_cast<std::uint64_t>(bounds.low_ns);
            const std::uint64_t high = static_cast<std::uint64_t>(bounds.high_ns);
            const bool crossed = bounds.high_ns < bounds.low_ns;
            const std::uint64_t width = crossed ? low - high : high - low;
            const std::uint64_t offset = crossed ? high + width / 2 : low + width / 2;
            return ClockPairing{reference_event_ns, reference_event_ns + offset, width - width / 2 + drift_ns};
        }
    } // namespace Observability
} // namespace SushiRuntime
