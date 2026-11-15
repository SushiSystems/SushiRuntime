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

#include <algorithm>
#include <initializer_list>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        PoolOccupancy::PoolOccupancy(const PartitionProblem& problem, std::size_t pool_count,
                                     const std::vector<std::optional<std::size_t>>& buffer_pool)
            : problem_(&problem),
              pool_count_(pool_count),
              slots_(std::max<std::size_t>(problem.nodes.size(), 1)),
              intervals_(problem.buffers.size() * pool_count),
              carried_in_(problem.buffers.size(), false),
              carried_out_(problem.buffers.size(), false),
              live_(pool_count, std::vector<std::int64_t>(slots_, 0)),
              used_(pool_count, 0)
        {
            std::vector<bool> written(problem.buffers.size(), false);
            std::vector<bool> touched(problem.buffers.size(), false);
            for (const PartitionNode& node : problem.nodes)
            {
                for (const std::size_t buffer : node.reads)
                {
                    if (!touched[buffer] && !written[buffer])
                        carried_in_[buffer] = true;
                    carried_out_[buffer] = false;
                }
                for (const std::size_t buffer : node.writes)
                {
                    written[buffer] = true;
                    carried_out_[buffer] = true;
                }
                for (const std::vector<std::size_t>* list : {&node.reads, &node.writes})
                {
                    for (const std::size_t buffer : *list)
                        touched[buffer] = true;
                }
            }

            const Interval whole{0, slots_ - 1};
            for (std::size_t buffer = 0; buffer < buffer_pool.size(); ++buffer)
            {
                if (!buffer_pool[buffer])
                    continue;
                intervals_[buffer * pool_count_ + *buffer_pool[buffer]] = whole;
                apply(live_[*buffer_pool[buffer]], whole, static_cast<std::int64_t>(problem.buffers[buffer].bytes));
            }
            for (std::size_t pool = 0; pool < pool_count_; ++pool)
                used_[pool] = peak(live_[pool]);
        }

        std::uint64_t PoolOccupancy::added_bytes(std::size_t node, std::size_t pool) const
        {
            std::vector<std::int64_t> live = live_[pool];
            apply_node(live, node, pool);
            return peak(live) - used_[pool];
        }

        void PoolOccupancy::add(std::size_t node, std::size_t pool)
        {
            apply_node(live_[pool], node, pool);
            const PartitionNode& entry = problem_->nodes[node];
            for (const std::vector<std::size_t>* list : {&entry.reads, &entry.writes})
            {
                for (const std::size_t buffer : *list)
                    intervals_[buffer * pool_count_ + pool] = widened(buffer, pool, node);
            }
            used_[pool] = peak(live_[pool]);
        }

        PoolOccupancy::Interval PoolOccupancy::widened(std::size_t buffer, std::size_t pool, std::size_t node) const
        {
            Interval interval{node, node};
            const std::optional<Interval>& held = intervals_[buffer * pool_count_ + pool];
            if (held)
            {
                interval.first = std::min(interval.first, held->first);
                interval.last = std::max(interval.last, held->last);
            }
            if (carried_in_[buffer])
                interval.first = 0;
            if (carried_out_[buffer])
                interval.last = slots_ - 1;
            return interval;
        }

        void PoolOccupancy::apply(std::vector<std::int64_t>& live, const Interval& interval, std::int64_t bytes)
        {
            for (std::size_t slot = interval.first; slot <= interval.last; ++slot)
                live[slot] += bytes;
        }

        std::uint64_t PoolOccupancy::peak(const std::vector<std::int64_t>& live)
        {
            std::int64_t highest = 0;
            for (const std::int64_t bytes : live)
                highest = std::max(highest, bytes);
            return static_cast<std::uint64_t>(highest);
        }

        void PoolOccupancy::apply_node(std::vector<std::int64_t>& live, std::size_t node, std::size_t pool) const
        {
            // A buffer a node lists twice is counted once.
            std::vector<bool> seen(problem_->buffers.size(), false);
            const PartitionNode& entry = problem_->nodes[node];
            for (const std::vector<std::size_t>* list : {&entry.reads, &entry.writes})
            {
                for (const std::size_t buffer : *list)
                {
                    if (seen[buffer])
                        continue;
                    const std::optional<Interval>& held = intervals_[buffer * pool_count_ + pool];
                    const Interval grown = widened(buffer, pool, node);
                    const auto bytes = static_cast<std::int64_t>(problem_->buffers[buffer].bytes);
                    if (held)
                        apply(live, *held, -bytes);
                    apply(live, grown, bytes);
                    seen[buffer] = true;
                }
            }
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
