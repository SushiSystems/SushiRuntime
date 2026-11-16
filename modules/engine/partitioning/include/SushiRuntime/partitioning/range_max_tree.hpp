/**************************************************************************/
/* range_max_tree.hpp                                                     */
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

/**
 * @file range_max_tree.hpp
 * @brief Declares RangeMaxTree, slots that take an addition over a range and answer the largest slot.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Holds a row of signed slots, all zero at first, with range addition and range maximum.
         *
         * Each addition and each query costs O(log n); peak_with() answers the peak under further
         * additions without applying them, in O(k log k + k log n) for k additions.
         */
        class SUSHIRUNTIME_API RangeMaxTree
        {
            public:
                /** @brief Holds an addition of value to every slot from first to last. */
                struct Delta
                {
                    std::size_t first = 0;   /**< First slot. */
                    std::size_t last = 0;    /**< Last slot; not below first. */
                    std::int64_t value = 0;  /**< Amount added. */
                };

                /** @brief Builds a row of slots zeros; a count of zero gives one slot. */
                explicit RangeMaxTree(std::size_t slots);

                /** @brief Adds delta.value to every slot from delta.first to delta.last. */
                void add(const Delta& delta);

                /** @brief Returns the largest slot from first to last. */
                [[nodiscard]] std::int64_t max(std::size_t first, std::size_t last) const;

                /** @brief Returns the largest slot of the row. */
                [[nodiscard]] std::int64_t max_all() const
                {
                    return max_[1];
                }

                /** @brief Returns the largest slot the row would hold once every delta were added. */
                [[nodiscard]] std::int64_t peak_with(const std::vector<Delta>& deltas) const;

            private:
                /** @brief Adds value over [first, last] below tree node vertex, which covers [low, high]. */
                void add(std::size_t vertex, std::size_t low, std::size_t high, std::size_t first, std::size_t last,
                         std::int64_t value);

                /** @brief Returns the largest slot of [first, last] below vertex, which covers [low, high]. */
                [[nodiscard]] std::int64_t max(std::size_t vertex, std::size_t low, std::size_t high, std::size_t first,
                                               std::size_t last) const;

                /// @brief Number of slots.
                std::size_t slots_;

                /// @brief Largest slot below each vertex, its own pending addition included.
                std::vector<std::int64_t> max_;

                /// @brief Addition pending on every slot below each vertex.
                std::vector<std::int64_t> pending_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
