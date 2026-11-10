/**************************************************************************/
/* residency.hpp                                                          */
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
 * @file residency.hpp
 * @brief Declares Residency, the place a Buffer or State keeps its memory, and its host-access rule.
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Names where a Buffer or State keeps its memory across steps.
     * @see modules/surface/api/include/SushiRuntime/api/data/README.md
     */
    enum class Residency : std::uint8_t
    {
        Shared,  /**< Host-addressable USM; the default. */
        Device,  /**< Device-resident USM; host access only via read/write_range. */
        Host     /**< Pinned host USM; host-readable while kernels run. */
    };

    /**
     * @brief Names a residency as its enumerator spelling, for messages.
     * @param residency The residency to name.
     * @return "Shared", "Device", "Host", or "unknown" for an out-of-range value.
     */
    inline const char* residency_name(Residency residency) noexcept
    {
        switch (residency)
        {
            case Residency::Shared: return "Shared";
            case Residency::Device: return "Device";
            case Residency::Host:   return "Host";
        }
        return "unknown";
    }

    namespace Detail
    {
        /**
         * @brief Returns whether the host may index memory of a residency directly.
         * @param residency The residency to test.
         * @return True for Shared and Host; false for Device.
         */
        [[nodiscard]] constexpr bool is_host_addressable(Residency residency) noexcept
        {
            return residency != Residency::Device;
        }
    } // namespace Detail
} // namespace SushiRuntime
