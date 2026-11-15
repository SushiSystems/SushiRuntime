/**************************************************************************/
/* coherence_intervals.cpp                                                */
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
 * @file coherence_intervals.cpp
 * @brief Implements CoherenceIntervals.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/coherence_intervals.hpp>

#include <algorithm>
#include <iterator>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        void CoherenceIntervals::reset(std::size_t size, MemorySpaceID home)
        {
            intervals_.clear();
            last_version_ = 0;
            if (size == 0)
                return;
            const std::uint64_t mask =
                home.value() < K_MAX_MEMORY_SPACES ? std::uint64_t{1} << home.value() : 0;
            intervals_.push_back(CoherenceInterval{0, size, mask, 0});
        }

        void CoherenceIntervals::clear() noexcept
        {
            intervals_.clear();
            last_version_ = 0;
        }

        std::size_t CoherenceIntervals::split_at(std::size_t offset)
        {
            const auto after = std::upper_bound(
                intervals_.begin(), intervals_.end(), offset,
                [](std::size_t value, const CoherenceInterval& interval) { return value < interval.begin; });
            if (after == intervals_.begin())
                return 0;
            const auto containing = std::prev(after);
            const std::size_t index = static_cast<std::size_t>(containing - intervals_.begin());
            if (offset == containing->begin)
                return index;
            if (offset >= containing->end)
                return index + 1;

            CoherenceInterval tail = *containing;
            tail.begin = offset;
            containing->end = offset;
            intervals_.insert(intervals_.begin() + static_cast<std::ptrdiff_t>(index + 1), tail);
            return index + 1;
        }

        void CoherenceIntervals::merge_around(std::size_t first, std::size_t last)
        {
            if (intervals_.empty())
                return;
            const std::size_t low = first == 0 ? 0 : first - 1;
            const std::size_t high = std::min(last + 1, intervals_.size() - 1);

            std::size_t kept = low;
            for (std::size_t index = low + 1; index <= high; ++index)
            {
                if (intervals_[kept].same_state(intervals_[index]))
                    intervals_[kept].end = intervals_[index].end;
                else
                    intervals_[++kept] = intervals_[index];
            }
            intervals_.erase(intervals_.begin() + static_cast<std::ptrdiff_t>(kept + 1),
                             intervals_.begin() + static_cast<std::ptrdiff_t>(high + 1));
        }

        std::uint64_t CoherenceIntervals::record_write(std::size_t begin, std::size_t end,
                                                       MemorySpaceID space)
        {
            end = std::min(end, size());
            if (begin >= end || space.value() >= K_MAX_MEMORY_SPACES)
                return 0;

            const std::size_t first = split_at(begin);
            const std::size_t past = split_at(end);
            const std::uint64_t version = ++last_version_;
            intervals_[first] =
                CoherenceInterval{begin, end, std::uint64_t{1} << space.value(), version};
            intervals_.erase(intervals_.begin() + static_cast<std::ptrdiff_t>(first + 1),
                             intervals_.begin() + static_cast<std::ptrdiff_t>(past));
            merge_around(first, first);
            return version;
        }

        void CoherenceIntervals::record_copy(std::size_t begin, std::size_t end, MemorySpaceID space)
        {
            end = std::min(end, size());
            if (begin >= end || space.value() >= K_MAX_MEMORY_SPACES)
                return;

            const std::size_t first = split_at(begin);
            const std::size_t past = split_at(end);
            const std::uint64_t bit = std::uint64_t{1} << space.value();
            for (std::size_t index = first; index < past; ++index)
                intervals_[index].valid_spaces |= bit;
            merge_around(first, past - 1);
        }

        void CoherenceIntervals::drop_space(MemorySpaceID space)
        {
            if (space.value() >= K_MAX_MEMORY_SPACES || intervals_.empty())
                return;
            const std::uint64_t bit = std::uint64_t{1} << space.value();
            for (CoherenceInterval& interval : intervals_)
                interval.valid_spaces &= ~bit;
            merge_around(0, intervals_.size() - 1);
        }

        std::uint64_t CoherenceIntervals::valid_spaces_at(std::size_t offset) const noexcept
        {
            const auto after = std::upper_bound(
                intervals_.begin(), intervals_.end(), offset,
                [](std::size_t value, const CoherenceInterval& interval) { return value < interval.begin; });
            if (after == intervals_.begin())
                return 0;
            const auto containing = std::prev(after);
            return offset < containing->end ? containing->valid_spaces : 0;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
