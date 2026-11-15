/**************************************************************************/
/* node_split.hpp                                                         */
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
 * @file node_split.hpp
 * @brief Declares NodeSplit and SplitPart, how a node states that it may run in parts.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names one part of a split node: the range of the axis it covers and its place in slice order.
     * @see modules/foundation/core/README.md
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

    /**
     * @brief States that a node may be cut along one axis into parts the runtime places.
     *
     * The runtime picks how many parts there are and where each runs; the consumer owns the
     * kernels. The slice runs once per part with that part's range, and the combine, when given,
     * joins the parts in slice order. A node states it through NodeOptions::split.
     * @see modules/foundation/core/README.md
     */
    struct NodeSplit
    {
        std::uint32_t axis = 0;   /**< The axis to cut along, an index the consumer defines. */
        std::uint64_t extent = 0; /**< Length of that axis; the parts cover [0, extent). */

        /** @brief Runs the node's work over one part. */
        std::function<void(const SplitPart&)> slice;

        /** @brief Joins the parts, given in slice order; empty when the parts need no join. */
        std::function<void(span<const SplitPart>)> combine;
    };
} // namespace SushiRuntime
