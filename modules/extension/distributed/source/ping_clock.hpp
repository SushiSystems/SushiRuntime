/**************************************************************************/
/* ping_clock.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file ping_clock.hpp
 * @brief Declares the clock a PING's token is read from and its round trip is measured on.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Returns the token of a PING sent at this instant.
         *
         * The token is the steady clock in microseconds, so only the process that made it
         * can measure it.
         */
        [[nodiscard]] inline std::uint64_t make_ping_token() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }

        /**
         * @brief Returns the microseconds since a token was made, as a PONG reports them.
         * @param token What make_ping_token() returned in this process.
         * @return 0 for a token from the future; the largest u32 for a longer round trip.
         */
        [[nodiscard]] inline std::uint32_t ping_round_trip_micros(std::uint64_t token) noexcept
        {
            const std::uint64_t now = make_ping_token();
            const std::uint64_t elapsed = now >= token ? now - token : 0;
            const std::uint64_t upper = std::numeric_limits<std::uint32_t>::max();
            return static_cast<std::uint32_t>(std::min(elapsed, upper));
        }

    } // namespace Distributed
} // namespace SushiRuntime
