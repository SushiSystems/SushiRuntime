/**************************************************************************/
/* steady_time.hpp                                                        */
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
 * @file steady_time.hpp
 * @brief Declares the steady-clock readings a dispatch record and a clock pairing are made of.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /**
         * @brief Returns @p t as nanoseconds since the steady clock's epoch.
         * @param t The time point.
         * @return Nanoseconds since the steady clock's epoch.
         */
        inline std::uint64_t steady_ns(std::chrono::steady_clock::time_point t) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
        }

        /** @brief Returns the steady clock's reading now, in nanoseconds since its epoch. */
        inline std::uint64_t steady_now_ns() noexcept
        {
            return steady_ns(std::chrono::steady_clock::now());
        }
    } // namespace Observability
} // namespace SushiRuntime
