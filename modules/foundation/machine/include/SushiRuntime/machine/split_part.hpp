/**************************************************************************/
/* split_part.hpp                                                         */
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
 * @file split_part.hpp
 * @brief Declares SplitPart, the range and slice-order place of one part of a split node.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Names one part of a split node: the range of the axis it covers and its place in slice order.
     * @see modules/foundation/machine/README.md
     */
    struct SplitPart
    {
        std::uint32_t axis = 0;  /**< The axis the node is cut along, as the consumer numbers it. */
        std::uint64_t begin = 0; /**< First index of the axis this part covers. */
        std::uint64_t end = 0;   /**< One past the last index this part covers. */
        std::size_t index = 0;   /**< Position of the part in slice order. */
        std::size_t count = 0;   /**< How many parts the node was cut into. */

        /** @brief Returns whether every field is equal. */
        [[nodiscard]] constexpr bool operator==(const SplitPart& other) const noexcept
        {
            return axis == other.axis && begin == other.begin && end == other.end &&
                   index == other.index && count == other.count;
        }

        /** @brief Returns whether a field differs. */
        [[nodiscard]] constexpr bool operator!=(const SplitPart& other) const noexcept
        {
            return !(*this == other);
        }
    };
} // namespace SushiRuntime
