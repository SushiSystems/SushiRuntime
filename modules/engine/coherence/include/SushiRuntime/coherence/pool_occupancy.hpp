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
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/coherence/partition_problem.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Counts the bytes of the instances a placement puts in each pool, each buffer once per pool.
         *
         * A node placed in a pool gives every buffer it touches an instance there. A buffer with a
         * stated device holds its home instance in that device's pool from the start. Every instance
         * is counted live for the whole step, which bounds the peak from above.
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

                /** @brief Returns the bytes placing node in pool would add. */
                [[nodiscard]] std::uint64_t added_bytes(std::size_t node, std::size_t pool) const;

                /** @brief Gives every buffer node touches an instance in pool. */
                void add(std::size_t node, std::size_t pool);

                /** @brief Returns the bytes held in pool. */
                [[nodiscard]] std::uint64_t used(std::size_t pool) const
                {
                    return used_[pool];
                }

            private:
                /// @brief The problem the counts belong to.
                const PartitionProblem* problem_;

                /// @brief Number of pools.
                std::size_t pool_count_;

                /// @brief Whether buffer b has an instance in pool p, at b * pool_count_ + p.
                std::vector<bool> present_;

                /// @brief Bytes held in each pool.
                std::vector<std::uint64_t> used_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
