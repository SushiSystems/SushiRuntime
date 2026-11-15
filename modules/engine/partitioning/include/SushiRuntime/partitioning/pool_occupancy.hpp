/**************************************************************************/
/* pool_occupancy.hpp                                                     */
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
 * @file pool_occupancy.hpp
 * @brief Declares PoolOccupancy, the bytes a partial placement holds in each memory pool.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/partitioning/partition_problem.hpp>
#include <SushiRuntime/partitioning/range_max_tree.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Counts the peak bytes of the live instances a placement puts in each pool.
         *
         * An instance is live from the first to the last node of its pool that touches the buffer.
         * A carried buffer (read before any write, or last touched by a write) and a stated home
         * are live for the whole step. Nodes may be added in any order.
         */
        class PoolOccupancy
        {
            public:
                /**
                 * @brief Builds an empty placement over pool_count pools.
                 * @param problem     The problem; must outlive the occupancy.
                 * @param buffer_pool Pool of each buffer's stated home, or nothing for a planned buffer.
                 */
                PoolOccupancy(const PartitionProblem& problem, std::size_t pool_count,
                              const std::vector<std::optional<std::size_t>>& buffer_pool);

                /** @brief Returns how far placing node in pool would raise the pool's peak, in O(k log n) for k buffers. */
                [[nodiscard]] std::uint64_t added_bytes(std::size_t node, std::size_t pool) const;

                /** @brief Gives every buffer node touches an instance in pool, live through node. */
                void add(std::size_t node, std::size_t pool);

                /** @brief Returns the peak bytes live in pool over the step. */
                [[nodiscard]] std::uint64_t used(std::size_t pool) const
                {
                    return used_[pool];
                }

            private:
                /** @brief Holds the first and last node an instance is live through. */
                struct Interval
                {
                    std::size_t first = 0; /**< First node, in recording order. */
                    std::size_t last = 0;  /**< Last node, in recording order. */
                };

                /** @brief Returns the interval of buffer's instance in pool once it also covers node. */
                [[nodiscard]] Interval widened(std::size_t buffer, std::size_t pool, std::size_t node) const;

                /** @brief Returns the distinct buffers node touches, in index order. */
                [[nodiscard]] std::vector<std::size_t> buffers_of(std::size_t node) const;

                /** @brief Returns the changes to the pool's slots once node's buffers are live there. */
                [[nodiscard]] std::vector<RangeMaxTree::Delta> deltas_of(std::size_t node, std::size_t pool) const;

                /// @brief The problem the counts belong to.
                const PartitionProblem* problem_;

                /// @brief Number of pools.
                std::size_t pool_count_;

                /// @brief Number of slots of a step: one per node, at least one.
                std::size_t slots_;

                /// @brief Live interval of buffer b in pool p, at b * pool_count_ + p.
                std::vector<std::optional<Interval>> intervals_;

                /// @brief Whether each buffer is read before any node writes it.
                std::vector<bool> carried_in_;

                /// @brief Whether each buffer is written by the last node that touches it.
                std::vector<bool> carried_out_;

                /// @brief Live bytes of each pool at each node.
                std::vector<RangeMaxTree> live_;

                /// @brief Peak live bytes of each pool.
                std::vector<std::uint64_t> used_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
