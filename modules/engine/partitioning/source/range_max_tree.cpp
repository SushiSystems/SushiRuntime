/**************************************************************************/
/* range_max_tree.cpp                                                     */
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
 * @file range_max_tree.cpp
 * @brief Implements RangeMaxTree.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/range_max_tree.hpp>

#include <algorithm>
#include <limits>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        RangeMaxTree::RangeMaxTree(std::size_t slots)
            : slots_(std::max<std::size_t>(slots, 1)), max_(4 * slots_, 0), pending_(4 * slots_, 0)
        {

        }

        void RangeMaxTree::add(const Delta& delta)
        {
            add(1, 0, slots_ - 1, delta.first, delta.last, delta.value);
        }

        std::int64_t RangeMaxTree::max(std::size_t first, std::size_t last) const
        {
            return max(1, 0, slots_ - 1, first, last);
        }

        std::int64_t RangeMaxTree::peak_with(const std::vector<Delta>& deltas) const
        {
            std::vector<std::size_t> cuts{0};
            cuts.reserve(2 * deltas.size() + 1);
            for (const Delta& delta : deltas)
            {
                cuts.push_back(delta.first);
                if (delta.last + 1 < slots_)
                    cuts.push_back(delta.last + 1);
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

            std::int64_t peak = std::numeric_limits<std::int64_t>::min();
            for (std::size_t index = 0; index < cuts.size(); ++index)
            {
                const std::size_t first = cuts[index];
                const std::size_t last = index + 1 < cuts.size() ? cuts[index + 1] - 1 : slots_ - 1;
                std::int64_t added = 0;
                for (const Delta& delta : deltas)
                {
                    if (delta.first <= first && first <= delta.last)
                        added += delta.value;
                }
                peak = std::max(peak, max(first, last) + added);
            }
            return peak;
        }

        void RangeMaxTree::add(std::size_t vertex, std::size_t low, std::size_t high, std::size_t first,
                               std::size_t last, std::int64_t value)
        {
            if (last < low || high < first)
                return;
            if (first <= low && high <= last)
            {
                max_[vertex] += value;
                pending_[vertex] += value;
                return;
            }
            const std::size_t middle = low + (high - low) / 2;
            add(2 * vertex, low, middle, first, last, value);
            add(2 * vertex + 1, middle + 1, high, first, last, value);
            max_[vertex] = std::max(max_[2 * vertex], max_[2 * vertex + 1]) + pending_[vertex];
        }

        std::int64_t RangeMaxTree::max(std::size_t vertex, std::size_t low, std::size_t high, std::size_t first,
                                       std::size_t last) const
        {
            if (first <= low && high <= last)
                return max_[vertex];
            const std::size_t middle = low + (high - low) / 2;
            std::int64_t best = std::numeric_limits<std::int64_t>::min();
            if (first <= middle)
                best = std::max(best, max(2 * vertex, low, middle, first, last));
            if (middle < last)
                best = std::max(best, max(2 * vertex + 1, middle + 1, high, first, last));
            return best + pending_[vertex];
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
