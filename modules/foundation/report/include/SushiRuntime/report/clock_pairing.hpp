/**************************************************************************/
/* clock_pairing.hpp                                                      */
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
 * @file clock_pairing.hpp
 * @brief Declares ClockPairing and DeviceClock, which place a device's event clock on the steady clock.
 * @author Mustafa Garip
 * @see modules/foundation/report/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Pairs one event clock reading with one steady clock reading taken beside it.
     *
     * Places the records of one device on the steady clock. A run with no device submission
     * carries the identity pairing: `event_ns == steady_ns` and `uncertainty_ns == 0`.
     */
    struct ClockPairing
    {
        std::uint64_t event_ns       = 0; /**< Event-clock reading of the device's first submission in the run. */
        std::uint64_t steady_ns      = 0; /**< Steady-clock reading taken beside event_ns. */
        std::uint64_t uncertainty_ns = 0; /**< Steady-clock spread across the submit call that produced the pairing. */
    };

    /** @brief Holds the pairing of one device's event clock for one run. */
    struct DeviceClock
    {
        std::uint32_t device_index = 0; /**< The device whose records the pairing converts. */
        ClockPairing  pairing{};        /**< The device's event clock against the steady clock. */
    };
} // namespace SushiRuntime
