/**************************************************************************/
/* split_sizing.hpp                                                       */
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
 * @file split_sizing.hpp
 * @brief Declares SizedPart and size_split(), which cut an axis into parts sized by device speed.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/machine/split_part.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds one part of a cut axis and the device it is sized for. */
        struct SizedPart
        {
            std::size_t device = 0; /**< Index of the device in the order size_split() was given. */
            SplitPart part;         /**< Range of the axis and place in slice order. */
        };

        /**
         * @brief Cuts [0, extent) of axis into at most one part per device, sized by device speed.
         *
         * Shares of the speeds (one over device_times) round down; the rest goes to the fastest
         * device, the first of equals. A device with no finite positive time gets no part.
         * @param device_times Seconds the whole node takes on each device.
         * @return The parts in device order; empty when extent is zero or no device can run the node.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<SizedPart> size_split(std::uint32_t axis, std::uint64_t extent,
                                                        const std::vector<double>& device_times);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
