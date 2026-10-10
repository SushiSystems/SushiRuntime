/**************************************************************************/
/* counting_sort.hpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file counting_sort.hpp
 * @brief Declares counting_sort(), a stable argsort of uint32 keys into buckets.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/detail/fixed_order_reduce.hpp>
#include <SushiRuntime/api/detail/fixed_order_scan.hpp>
#include <SushiRuntime/api/detail/late_count_resolution.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/device_sized.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace Primitives
        {
            /**
             * @brief Stably argsorts keys by 8-bit digits into bucket offsets.
             * @throws Core::SushiException If a buffer is too small, if @p keys,
             *        @p indices, @p offsets or @p counts sit on different
             *        devices, if @p key_range.capacity is zero, or at run if
             *        either live count exceeds its capacity.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            inline void counting_sort(Recorder& recorder,
                                      const Buffer<std::uint32_t>& keys,
                                      const Buffer<std::uint32_t>& indices,
                                      const Buffer<std::uint32_t>& offsets,
                                      const Buffer<std::uint32_t>& total,
                                      LateCount n, LateCount key_range,
                                      const Buffer<std::uint32_t>* counts = nullptr)
            {
                detail::validate_late_count(n, "add_counting_sort");
                detail::validate_late_count(key_range, "add_counting_sort");
                if (key_range.capacity == 0)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: key_range.capacity is zero."});
                if (n.capacity > keys.size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: n.capacity (" + std::to_string(n.capacity) +
                        ") exceeds the keys buffer's size (" +
                        std::to_string(keys.size()) + ")."});
                if (indices.size() < n.capacity)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: indices holds " +
                        std::to_string(indices.size()) + " elements but " +
                        std::to_string(n.capacity) + " are required."});
                if (offsets.size() < key_range.capacity + 1)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: offsets holds " +
                        std::to_string(offsets.size()) + " elements but " +
                        std::to_string(key_range.capacity + 1) + " are required."});
                if (total.size() == 0)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: the total buffer holds no elements."});
                if (counts != nullptr && counts->size() < key_range.capacity)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: counts holds " +
                        std::to_string(counts->size()) + " elements but " +
                        std::to_string(key_range.capacity) + " are required."});
                if (indices.device_index() != keys.device_index() ||
                    offsets.device_index() != keys.device_index() ||
                    (counts != nullptr && counts->device_index() != keys.device_index()))
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_counting_sort: keys, indices, offsets and counts must "
                        "live on the same device."});

                constexpr std::size_t DIGIT_BINS = detail::RADIX_DIGIT_BINS;
                const std::size_t digit_count = detail::radix_digit_count(key_range.capacity);

                // Every node this call records belongs to one rule.
                auto rule = recorder.rule();

                // n_state[0] is the live element count, n_state[1] its live tile count.
                std::uint32_t* n_state = recorder.scratch<std::uint32_t>(
                    2, keys.device_index(), keys.residency());
                std::uint32_t* n_state_ptr = n_state;
                const Core::ResourceRegion n_state_region{n_state_ptr, 0,
                                                            2 * sizeof(std::uint32_t)};

                // Per digit, the plan holds live work counts and where it places.
                std::uint32_t* plan_ptr = nullptr;
                if (n.capacity != 0)
                {
                    std::uint32_t* plan = recorder.scratch<std::uint32_t>(
                        digit_count * detail::RADIX_PLAN_WORDS, keys.device_index(),
                        keys.residency());
                    plan_ptr = plan;
                }
                const Core::ResourceRegion plan_region{plan_ptr, 0,
                    digit_count * detail::RADIX_PLAN_WORDS * sizeof(std::uint32_t)};

                // One kernel resolves both counts, publishes total and fills the digit plan.
                std::uint32_t* total_base = total.data();
                recorder.resolve(std::array<LateCount, 2>{n, key_range},
                    keys.device_index(),
                    Access(Reads(), (plan_ptr != nullptr)
                                        ? Writes(n_state_region, total, plan_region)
                                        : Writes(n_state_region, total)),
                    [n_state_ptr, total_base, plan_ptr, digit_count](
                        const std::array<std::size_t, 2>& live)
                    {
                        const std::size_t tiles = detail::next_live_tile_count(live[0]);
                        n_state_ptr[0] = static_cast<std::uint32_t>(live[0]);
                        n_state_ptr[1] = static_cast<std::uint32_t>(tiles);
                        total_base[0] = static_cast<std::uint32_t>(live[0]);
                        if (plan_ptr != nullptr)
                            detail::fill_radix_plan(digit_count, live[0], tiles, live[1], plan_ptr);
                    },
                    NodeOptions{std::size_t{1}});

                if (n.capacity == 0)
                {
                    // No element ever arrives, so every bucket starts and stays at zero.
                    std::uint32_t* offsets_base = offsets.data();
                    std::uint32_t* counts_base = (counts != nullptr) ? counts->data() : nullptr;
                    Reads reads = Reads();
                    Writes writes = (counts != nullptr) ? Writes(offsets, *counts)
                                                        : Writes(offsets);
                    recorder.kernel(
                        DeviceSized{key_range.capacity + 1, nullptr, nullptr},
                        Access(reads, writes),
                        [offsets_base, counts_base, capacity = key_range.capacity](std::size_t k)
                        {
                            offsets_base[k] = 0;
                            if (counts_base != nullptr && k < capacity)
                                counts_base[k] = 0;
                        },
                        NodeOptions{key_range.capacity + 1});
                    return;
                }

                const std::size_t T_cap = detail::next_live_tile_count(n.capacity);
                constexpr std::size_t tile = detail::REDUCE_TILE;

                // hist holds raw counts until the column scan overwrites it in place with prefixes.
                std::uint32_t* hist = recorder.scratch<std::uint32_t>(
                    DIGIT_BINS * T_cap, keys.device_index(), keys.residency());
                std::uint32_t* col_total = recorder.scratch<std::uint32_t>(
                    DIGIT_BINS, keys.device_index(), keys.residency());
                std::uint32_t* bin_base = recorder.scratch<std::uint32_t>(
                    DIGIT_BINS, keys.device_index(), keys.residency());

                // One scratch pings against the indices; a single digit never needs it.
                std::uint32_t* scratch_base = nullptr;
                if (digit_count >= 2)
                {
                    std::uint32_t* owned = recorder.scratch<std::uint32_t>(
                        n.capacity, keys.device_index(), keys.residency());
                    scratch_base = owned;
                }

                const std::uint32_t* keys_base = keys.data();
                std::uint32_t* indices_base = indices.data();
                std::uint32_t* hist_base = hist;
                std::uint32_t* col_total_base = col_total;
                std::uint32_t* bin_base_base = bin_base;

                const Core::ResourceRegion indices_region{static_cast<void*>(indices_base), 0,
                    n.capacity * sizeof(std::uint32_t)};
                const Core::ResourceRegion scratch_region{static_cast<void*>(scratch_base), 0,
                    n.capacity * sizeof(std::uint32_t)};
                const Core::ResourceRegion hist_region{static_cast<void*>(hist_base), 0,
                    DIGIT_BINS * T_cap * sizeof(std::uint32_t)};
                const Core::ResourceRegion col_total_region{static_cast<void*>(col_total_base), 0,
                    DIGIT_BINS * sizeof(std::uint32_t)};
                const Core::ResourceRegion bin_base_region{static_cast<void*>(bin_base_base), 0,
                    DIGIT_BINS * sizeof(std::uint32_t)};

                // Index of one word of one digit's plan.
                const auto plan_word = [](std::size_t digit, std::size_t word) {
                    return digit * detail::RADIX_PLAN_WORDS + word;
                };

                for (std::size_t d = 0; d < digit_count; ++d)
                {
                    const bool first = (d == 0);
                    const std::uint32_t shift = static_cast<std::uint32_t>(8 * d);
                    std::uint32_t* digit_plan = plan_ptr + d * detail::RADIX_PLAN_WORDS;
                    const std::uint32_t* snapshot = digit_plan + detail::RADIX_PLAN_LIVE_N;

                    // A later digit reads whichever buffer the previous digit filled.
                    Reads sources = first ? Reads()
                                          : (scratch_base != nullptr
                                                ? Reads(indices_region, scratch_region)
                                                : Reads(indices_region));

                    // Histogram: each tile overwrites its own row, so no run clears it.
                    {
                        Reads reads = Reads(keys, plan_region);
                        for (const Core::ResourceRegion& r : sources.keys)
                            reads.keys.push_back(r);
                        Writes writes = Writes(hist_region);
                        recorder.kernel(
                            DeviceSized{T_cap, plan_ptr + plan_word(d, detail::RADIX_PLAN_TILES),
                                        snapshot},
                            Access(reads, writes),
                            [keys_base, indices_base, scratch_base, hist_base, first, shift,
                             T_cap](std::size_t t, std::size_t live_n, std::size_t into_indices)
                            {
                                const std::uint32_t* src_indices =
                                    into_indices != 0 ? scratch_base : indices_base;
                                std::uint32_t local[DIGIT_BINS] = {};
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > live_n) end = live_n;
                                for (std::size_t i = begin; i < end; ++i)
                                {
                                    const std::size_t src = first ? i : src_indices[i];
                                    const std::uint32_t digit =
                                        (keys_base[src] >> shift) & 0xFFu;
                                    ++local[digit];
                                }
                                for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                    hist_base[b * T_cap + t] = local[b];
                            },
                            NodeOptions{T_cap});
                    }

                    // Column scan: one work-item per bin scans its tiles left to right.
                    {
                        // In place over hist: exactly one work-item touches each bin's column.
                        Reads reads = Reads(hist_region, n_state_region, plan_region);
                        Writes writes = Writes(hist_region, col_total_region);
                        recorder.kernel(
                            DeviceSized{DIGIT_BINS,
                                        plan_ptr + plan_word(d, detail::RADIX_PLAN_BINS),
                                        n_state_ptr + 1},
                            Access(reads, writes),
                            [hist_base, col_total_base, T_cap](
                                std::size_t b, std::size_t live_T)
                            {
                                std::uint32_t running = 0;
                                for (std::size_t t = 0; t < live_T; ++t)
                                {
                                    const std::uint32_t count = hist_base[b * T_cap + t];
                                    hist_base[b * T_cap + t] = running;
                                    running += count;
                                }
                                col_total_base[b] = running;
                            },
                            NodeOptions{DIGIT_BINS});
                    }

                    // Bin base: one work-item walks the 256 bin totals, whatever n is.
                    {
                        Reads reads = Reads(col_total_region, plan_region);
                        Writes writes = Writes(bin_base_region);
                        recorder.kernel(
                            DeviceSized{std::size_t{1},
                                        plan_ptr + plan_word(d, detail::RADIX_PLAN_ONE),
                                        nullptr},
                            Access(reads, writes),
                            [col_total_base, bin_base_base](std::size_t)
                            {
                                std::uint32_t running = 0;
                                for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                {
                                    bin_base_base[b] = running;
                                    running += col_total_base[b];
                                }
                            },
                            NodeOptions{std::size_t{1}});
                    }

                    // Placement: tiles write in ascending index order, so it is stable.
                    {
                        Reads reads = Reads(keys, hist_region, bin_base_region, plan_region);
                        for (const Core::ResourceRegion& r : sources.keys)
                            reads.keys.push_back(r);
                        Writes writes = (scratch_base != nullptr)
                            ? Writes(indices_region, scratch_region)
                            : Writes(indices_region);
                        recorder.kernel(
                            DeviceSized{T_cap, plan_ptr + plan_word(d, detail::RADIX_PLAN_TILES),
                                        snapshot},
                            Access(reads, writes),
                            [keys_base, indices_base, scratch_base, hist_base, bin_base_base,
                             first, shift, T_cap](
                                std::size_t t, std::size_t live_n, std::size_t into_indices)
                            {
                                const std::uint32_t* src_indices =
                                    into_indices != 0 ? scratch_base : indices_base;
                                std::uint32_t* dst_indices =
                                    into_indices != 0 ? indices_base : scratch_base;
                                std::uint32_t cursor[DIGIT_BINS];
                                for (std::size_t b = 0; b < DIGIT_BINS; ++b)
                                    cursor[b] = bin_base_base[b] + hist_base[b * T_cap + t];
                                const std::size_t begin = t * tile;
                                std::size_t end = begin + tile;
                                if (end > live_n) end = live_n;
                                for (std::size_t i = begin; i < end; ++i)
                                {
                                    const std::size_t src = first ? i : src_indices[i];
                                    const std::uint32_t digit =
                                        (keys_base[src] >> shift) & 0xFFu;
                                    dst_indices[cursor[digit]++] =
                                        static_cast<std::uint32_t>(src);
                                }
                            },
                            NodeOptions{T_cap});
                    }
                }

                // Offsets: one lower_bound per bucket; no live bucket count is read.
                {
                    std::uint32_t* offsets_base = offsets.data();
                    Reads reads = Reads(keys, indices, n_state_region);
                    Writes writes = Writes(offsets);
                    recorder.kernel(
                        DeviceSized{key_range.capacity + 1, nullptr, n_state_ptr},
                        Access(reads, writes),
                        [keys_base, indices_base, offsets_base](
                            std::size_t k, std::size_t live_n)
                        {
                            std::size_t lo = 0;
                            std::size_t hi = live_n;
                            while (lo < hi)
                            {
                                const std::size_t mid = lo + (hi - lo) / 2;
                                if (keys_base[indices_base[mid]] < k)
                                    lo = mid + 1;
                                else
                                    hi = mid;
                            }
                            offsets_base[k] = static_cast<std::uint32_t>(lo);
                        },
                        NodeOptions{key_range.capacity + 1});
                }

                if (counts == nullptr)
                    return;

                // Counts are differences of two settled offsets and need no live scalar.
                std::uint32_t* offsets_base = offsets.data();
                std::uint32_t* counts_base = counts->data();
                Reads reads = Reads(offsets);
                Writes writes = Writes(*counts);
                recorder.kernel(
                    DeviceSized{key_range.capacity, nullptr, nullptr},
                    Access(reads, writes),
                    [offsets_base, counts_base](std::size_t k)
                    {
                        counts_base[k] = offsets_base[k + 1] - offsets_base[k];
                    },
                    NodeOptions{key_range.capacity});
            }
        } // namespace Primitives
    } // namespace API
} // namespace SushiRuntime
