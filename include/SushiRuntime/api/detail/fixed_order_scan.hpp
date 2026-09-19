/**************************************************************************/
/* fixed_order_scan.hpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/api/detail/fixed_order_reduce.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief Live per-level width of a tiled pass planned for a capacity.
             */
            struct LiveTileLevel
            {
                std::size_t in_count = 0;  /**< Live values this level folds. */
                std::size_t out_count = 0; /**< Live partials it produces. */
            };

            /**
             * @brief Lays out the levels a 256-tile tree has for a capacity.
             *
             * Reuses add_reduce's level plan so scan, radix and reduce all answer
             * "how many levels, which slice each folds" from one source.
             *
             * @param capacity Upper bound the caller's buffers are sized for.
             * @return The levels in execution order; empty when @p capacity is zero.
             */
            inline std::vector<ReduceLevel> plan_tiled_levels(std::size_t capacity)
            {
                return plan_reduction(capacity);
            }

            /**
             * @brief The one place the ceil(count / REDUCE_TILE) recurrence lives.
             * @param live_in Live element count entering a tiled level.
             * @return The live element count it produces.
             */
            inline std::size_t next_live_tile_count(std::size_t live_in) noexcept
            {
                return (live_in == 0) ? 0 : (live_in + REDUCE_TILE - 1) / REDUCE_TILE;
            }

            /**
             * @brief Fills a caller-owned array with a live count's per-level widths.
             *
             * Walks the same number of physical levels @p level_count as
             * plan_tiled_levels(capacity) planned, folding @p live_n at each level
             * exactly as plan_reduction(live_n) would. Once the running count reaches
             * one it stays there, so levels beyond what plan_reduction(live_n) would
             * have produced degenerate to a single-element pass-through fold and the
             * boundaries below that point are byte-identical to plan_reduction(live_n).
             *
             * Writes only through @p out, so a caller that already owns the levels
             * (a graph's per-run resolver, sized once at registration) can call this
             * every run with no allocation.
             *
             * @param level_count Number of levels, from plan_tiled_levels(capacity).size().
             * @param live_n      Live element count; must not exceed that capacity.
             * @param out         Array of at least @p level_count LiveTileLevel entries.
             */
            inline void fill_live_tile_levels(std::size_t level_count, std::size_t live_n,
                                              LiveTileLevel* out)
            {
                std::size_t count = live_n;
                for (std::size_t i = 0; i < level_count; ++i)
                {
                    out[i].in_count = count;
                    out[i].out_count = next_live_tile_count(count);
                    count = out[i].out_count;
                }
            }

            /**
             * @brief Writes a live count's per-level widths as flat words a kernel reads.
             * @param level_count Number of levels, from plan_tiled_levels(capacity).size().
             * @param live_n      Live element count; must not exceed that capacity.
             * @param out         2 * @p level_count words: level i's in_count at i, its out_count at level_count + i.
             */
            inline void fill_live_level_words(std::size_t level_count, std::size_t live_n,
                                              std::uint32_t* out) noexcept
            {
                std::size_t count = live_n;
                for (std::size_t i = 0; i < level_count; ++i)
                {
                    out[i] = static_cast<std::uint32_t>(count);
                    count = next_live_tile_count(count);
                    out[level_count + i] = static_cast<std::uint32_t>(count);
                }
            }

            /**
             * @brief Derives a live count's per-level widths over a capacity's levels.
             *
             * The allocating, vector-returning wrapper over fill_live_tile_levels();
             * fine for tests and other non-hot callers, but a per-run resolver should
             * call fill_live_tile_levels() directly into scratch it already owns.
             *
             * @param capacity_levels The plan from plan_tiled_levels(capacity).
             * @param live_n          Live element count; must not exceed capacity.
             * @return One LiveTileLevel per entry of @p capacity_levels.
             */
            inline std::vector<LiveTileLevel> live_tile_levels(
                const std::vector<ReduceLevel>& capacity_levels, std::size_t live_n)
            {
                std::vector<LiveTileLevel> live(capacity_levels.size());
                fill_live_tile_levels(capacity_levels.size(), live_n, live.data());
                return live;
            }

            /** @brief Bins one 8-bit radix digit sorts into. */
            constexpr std::size_t RADIX_DIGIT_BINS = 256;

            /** @brief Words each digit occupies in a radix plan. */
            constexpr std::size_t RADIX_PLAN_WORDS = 5;

            /** @brief Offset of a digit's live tile count, zero when it is skipped. */
            constexpr std::size_t RADIX_PLAN_TILES = 0;

            /** @brief Offset of a digit's live bin count, zero when it is skipped. */
            constexpr std::size_t RADIX_PLAN_BINS = 1;

            /** @brief Offset of a digit's single-item count, zero when it is skipped. */
            constexpr std::size_t RADIX_PLAN_ONE = 2;

            /** @brief Offset of the live element count, first word of a digit's snapshot. */
            constexpr std::size_t RADIX_PLAN_LIVE_N = 3;

            /** @brief Offset of the flag that sends a digit's placement into the indices. */
            constexpr std::size_t RADIX_PLAN_INTO_INDICES = 4;

            /**
             * @brief Counts the 8-bit digits that cover every key below a key range.
             * @param key_range Exclusive key bound; zero and one both take one digit.
             * @return At least one, at most four.
             */
            inline std::size_t radix_digit_count(std::size_t key_range) noexcept
            {
                std::size_t bits = 0;
                for (std::uint64_t v = (key_range > 1) ? key_range - 1 : 0; v != 0; v >>= 1)
                    ++bits;
                return (bits == 0) ? 1 : (bits + 7) / 8;
            }

            /**
             * @brief Writes one run's radix plan: which digits run and where each lands.
             *
             * Digits at or above radix_digit_count(@p live_key_range) get zero work
             * counts. Active digits alternate between the indices and the scratch so
             * the last active one always writes the indices.
             *
             * @param digit_count Digits planned at registration, from the key range capacity.
             * @param live_n      Live element count.
             * @param live_tiles  Live tile count, next_live_tile_count(@p live_n).
             * @param live_key_range Live key range; must not exceed the planned capacity.
             * @param out         Array of at least @p digit_count * RADIX_PLAN_WORDS words.
             */
            inline void fill_radix_plan(std::size_t digit_count, std::size_t live_n,
                                        std::size_t live_tiles, std::size_t live_key_range,
                                        std::uint32_t* out) noexcept
            {
                const std::size_t live_digits = radix_digit_count(live_key_range);
                for (std::size_t d = 0; d < digit_count; ++d)
                {
                    const bool active = d < live_digits;
                    std::uint32_t* word = out + d * RADIX_PLAN_WORDS;
                    word[RADIX_PLAN_TILES] = active ? static_cast<std::uint32_t>(live_tiles) : 0u;
                    word[RADIX_PLAN_BINS] = active ? static_cast<std::uint32_t>(RADIX_DIGIT_BINS) : 0u;
                    word[RADIX_PLAN_ONE] = active ? 1u : 0u;
                    word[RADIX_PLAN_LIVE_N] = static_cast<std::uint32_t>(live_n);
                    word[RADIX_PLAN_INTO_INDICES] =
                        (active && (live_digits - 1 - d) % 2 == 0) ? 1u : 0u;
                }
            }

        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
