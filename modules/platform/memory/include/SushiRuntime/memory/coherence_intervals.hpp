/**************************************************************************/
/* coherence_intervals.hpp                                                */
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
 * @file coherence_intervals.hpp
 * @brief Declares CoherenceIntervals, the map from byte ranges of a buffer to the spaces that hold them.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds the state of one byte range [begin, end) of a buffer. */
        struct CoherenceInterval
        {
            std::size_t begin = 0;          /**< First byte of the range. */
            std::size_t end = 0;            /**< One past the last byte of the range. */
            std::uint64_t valid_spaces = 0; /**< Bit i set when space i holds the current bytes. */
            std::uint64_t version = 0;      /**< Version of the bytes; raised by every write. */

            /** @brief Returns whether both intervals hold the same spaces and version. */
            [[nodiscard]] bool same_state(const CoherenceInterval& other) const noexcept
            {
                return valid_spaces == other.valid_spaces && version == other.version;
            }
        };

        /**
         * @brief Maps the byte ranges of one buffer to their valid spaces and version.
         *
         * The intervals are sorted, cover [0, size) without gaps and never leave two equal
         * neighbours apart. The map takes no lock of its own: the directory hands it out under
         * its entry's lock (LockedCoherence), and the planner works on a copy of it.
         */
        class SUSHIRUNTIME_API CoherenceIntervals final
        {
            public:
                /** @brief Number of spaces a valid-space bitmask can name. */
                static constexpr std::uint32_t K_MAX_MEMORY_SPACES = 64;

                /** @brief Makes the map one interval [0, size) valid on home at version 0. */
                void reset(std::size_t size, MemorySpaceID home);

                /** @brief Empties the map. */
                void clear() noexcept;

                /**
                 * @brief Records a write of [begin, end) on space at a new version.
                 *
                 * Every other space loses the range. The range is clipped to the buffer; an empty
                 * range or a space past K_MAX_MEMORY_SPACES changes nothing.
                 * @return The version the written bytes now carry, or 0 when nothing changed.
                 */
                std::uint64_t record_write(std::size_t begin, std::size_t end, MemorySpaceID space);

                /**
                 * @brief Records that space now holds the current bytes of [begin, end).
                 *
                 * A copy makes this call; the version of the bytes does not change.
                 */
                void record_copy(std::size_t begin, std::size_t end, MemorySpaceID space);

                /**
                 * @brief Records that space no longer holds any byte of the buffer.
                 *
                 * The versions do not change. A space past K_MAX_MEMORY_SPACES changes nothing.
                 */
                void drop_space(MemorySpaceID space);

                /**
                 * @brief Retires the history of the map at a horizon.
                 *
                 * Drops every version to 0 and joins each run of intervals that hold the same
                 * spaces. The valid spaces of every byte do not change, and a later write takes
                 * a version above every one handed out before.
                 * @return The number of intervals left.
                 */
                [[nodiscard]] std::size_t retire_history();

                /** @brief Returns the intervals, sorted by begin. */
                [[nodiscard]] const std::vector<CoherenceInterval>& intervals() const noexcept
                {
                    return intervals_;
                }

                /** @brief Returns the valid spaces of the byte at offset, or 0 past the end. */
                [[nodiscard]] std::uint64_t valid_spaces_at(std::size_t offset) const noexcept;

                /** @brief Returns the bytes the map covers. */
                [[nodiscard]] std::size_t size() const noexcept
                {
                    return intervals_.empty() ? 0 : intervals_.back().end;
                }

            private:
                /** @brief Splits so that offset begins an interval; returns that interval's index. */
                std::size_t split_at(std::size_t offset);

                /** @brief Joins equal neighbours in [first, last] and one past each side. */
                void merge_around(std::size_t first, std::size_t last);

                /// @brief Sorted intervals covering [0, size).
                std::vector<CoherenceInterval> intervals_;

                /// @brief Highest version handed out.
                std::uint64_t last_version_ = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
