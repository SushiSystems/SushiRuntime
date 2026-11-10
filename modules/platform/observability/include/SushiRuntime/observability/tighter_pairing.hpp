/**************************************************************************/
/* tighter_pairing.hpp                                                    */
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
 * @file tighter_pairing.hpp
 * @brief Declares tighter_pairing(), which picks the better of two clock pairings of one device.
 * @author Mustafa Garip
 * @see modules/platform/observability/README.md
 */

#pragma once

#include <SushiRuntime/report/clock_pairing.hpp>

namespace SushiRuntime
{
    namespace Internal::Observability
    {
        /**
         * @brief Returns the pairing with the smaller uncertainty, @p kept when the two are equal.
         *
         * A pairing's steady reading starts its submit window and its uncertainty is the window's
         * length, so the steady instant of its event reading lies within it. Every candidate is a
         * true bound on its own, so the shortest window is the tightest one.
         */
        [[nodiscard]] constexpr ClockPairing tighter_pairing(const ClockPairing& kept,
                                                             const ClockPairing& candidate) noexcept
        {
            return candidate.uncertainty_ns < kept.uncertainty_ns ? candidate : kept;
        }
    } // namespace Observability
} // namespace SushiRuntime
