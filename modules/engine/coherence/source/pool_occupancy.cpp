/**************************************************************************/
/* pool_occupancy.cpp                                                     */
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
 * @file pool_occupancy.cpp
 * @brief Implements PoolOccupancy.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/pool_occupancy.hpp>

#include <initializer_list>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PoolOccupancy::PoolOccupancy(const PartitionProblem& problem, std::size_t pool_count,
                                     const std::vector<std::optional<std::size_t>>& buffer_pool)
            : problem_(&problem),
              pool_count_(pool_count),
              present_(problem.buffers.size() * pool_count, false),
              used_(pool_count, 0)
        {
            for (std::size_t buffer = 0; buffer < buffer_pool.size(); ++buffer)
            {
                if (!buffer_pool[buffer])
                    continue;
                present_[buffer * pool_count_ + *buffer_pool[buffer]] = true;
                used_[*buffer_pool[buffer]] += problem.buffers[buffer].bytes;
            }
        }

        std::uint64_t PoolOccupancy::added_bytes(std::size_t node, std::size_t pool) const
        {
            std::vector<bool> counted(problem_->buffers.size(), false);
            std::uint64_t added = 0;
            const PartitionNode& entry = problem_->nodes[node];
            for (const std::vector<std::size_t>* list : {&entry.reads, &entry.writes})
            {
                for (const std::size_t buffer : *list)
                {
                    if (counted[buffer] || present_[buffer * pool_count_ + pool])
                        continue;
                    counted[buffer] = true;
                    added += problem_->buffers[buffer].bytes;
                }
            }
            return added;
        }

        void PoolOccupancy::add(std::size_t node, std::size_t pool)
        {
            const PartitionNode& entry = problem_->nodes[node];
            for (const std::vector<std::size_t>* list : {&entry.reads, &entry.writes})
            {
                for (const std::size_t buffer : *list)
                {
                    if (present_[buffer * pool_count_ + pool])
                        continue;
                    present_[buffer * pool_count_ + pool] = true;
                    used_[pool] += problem_->buffers[buffer].bytes;
                }
            }
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
