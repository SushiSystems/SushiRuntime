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
     * @brief Pairs one event clock reading with its best estimate on the steady clock.
     *
     * A reading t converts to `steady_ns + (t - event_ns)`, within `uncertainty_ns` of its true
     * steady time. A run with no device submission carries `event_ns == steady_ns`, no uncertainty.
     * @see modules/foundation/report/README.md
     */
    struct ClockPairing
    {
        std::uint64_t event_ns       = 0; /**< An event-clock reading of one of the run's dispatches on the device. */
        std::uint64_t steady_ns      = 0; /**< event_ns on the steady clock, at the midpoint of the bounds the run's dispatches allow. */
        std::uint64_t uncertainty_ns = 0; /**< Half the bounds' width plus the clocks' drift over the run; a converted reading is off by at most this. */
    };

    /** @brief Holds the pairing of one device's event clock for one run. */
    struct DeviceClock
    {
        std::uint32_t device_index = 0; /**< The device whose records the pairing converts. */
        ClockPairing  pairing{};        /**< The device's event clock against the steady clock. */
    };
} // namespace SushiRuntime
