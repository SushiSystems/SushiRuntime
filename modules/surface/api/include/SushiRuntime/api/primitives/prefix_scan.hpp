/**************************************************************************/
/* prefix_scan.hpp                                                        */
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
 * @file prefix_scan.hpp
 * @brief Declares prefix_scan(), an exclusive scan of uint32 counts into offsets.
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
             * @brief Scans uint32 counts into exclusive running offsets, late-bound.
             * @param offsets Destination; must hold at least n.capacity + 1 elements.
             * @throws Core::SushiException If a buffer is too small, if @p counts
             *        and @p offsets sit on different devices, or at run if the
             *        live count exceeds @p n.capacity.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            inline void prefix_scan(Recorder& recorder,
                                    const Buffer<std::uint32_t>& counts,
                                    const Buffer<std::uint32_t>& offsets,
                                    const Buffer<std::uint32_t>& total,
                                    LateCount n)
            {
                detail::validate_late_count(n, "add_prefix_scan");
                if (n.capacity > counts.size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_prefix_scan: n.capacity (" + std::to_string(n.capacity) +
                        ") exceeds the counts buffer's size (" +
                        std::to_string(counts.size()) + ")."});
                if (offsets.size() < n.capacity + 1)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_prefix_scan: offsets holds " +
                        std::to_string(offsets.size()) + " elements but " +
                        std::to_string(n.capacity + 1) + " are required."});
                if (total.size() == 0)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_prefix_scan: the total buffer holds no elements."});
                if (offsets.device_index() != counts.device_index())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_prefix_scan: counts and offsets must live on the same device."});

                const std::vector<detail::ReduceLevel> levels =
                    detail::plan_tiled_levels(n.capacity);
                std::uint32_t* counts_base = counts.data();
                std::uint32_t* offsets_base = offsets.data();
                std::uint32_t* total_base = total.data();

                if (levels.empty())
                {
                    // Capacity zero: the resolver checks the live count and writes an empty scan.
                    auto rule = recorder.rule();
                    recorder.resolve(std::array<LateCount, 1>{n}, counts.device_index(),
                        Access(Reads(), Writes(offsets, total)),
                        [offsets_base, total_base](const std::array<std::size_t, 1>&)
                        {
                            offsets_base[0] = 0;
                            total_base[0] = 0;
                        },
                        NodeOptions{std::size_t{1}});
                    return;
                }

                const std::size_t level_count = levels.size();
                const std::size_t up_scratch_count = detail::reduce_scratch_count(levels);
                const std::size_t down_scratch_count = detail::reduce_scratch_count(levels);

                // A single level needs no scratch; a zero-size allocation would fail.
                std::uint32_t* up_scratch = nullptr;
                if (up_scratch_count != 0)
                {
                    std::uint32_t* up = recorder.scratch<std::uint32_t>(
                        up_scratch_count, counts.device_index(), counts.residency());
                    up_scratch = up;
                }

                std::uint32_t* down_scratch = nullptr;
                if (down_scratch_count != 0)
                {
                    std::uint32_t* down = recorder.scratch<std::uint32_t>(
                        down_scratch_count, counts.device_index(), counts.residency());
                    down_scratch = down;
                }

                // state[i] is level i's live in_count; state[level_count+i] its out_count.
                const std::size_t state_words = 2 * level_count;
                std::uint32_t* state = recorder.scratch<std::uint32_t>(
                    state_words, counts.device_index(), counts.residency());
                std::uint32_t* state_ptr = state;
                const Core::ResourceRegion state_region{
                    state_ptr, 0, state_words * sizeof(std::uint32_t)};

                // Every node this call records belongs to one rule.
                auto rule = recorder.rule();

                recorder.resolve(std::array<LateCount, 1>{n}, counts.device_index(),
                    Access(Reads(), Writes(state_region)),
                    [state_ptr, level_count](const std::array<std::size_t, 1>& live)
                    {
                        detail::fill_live_level_words(level_count, live[0], state_ptr);
                    },
                    NodeOptions{std::size_t{1}});

                void* up_key = static_cast<void*>(up_scratch);
                void* down_key = static_cast<void*>(down_scratch);
                constexpr std::size_t tile = detail::REDUCE_TILE;

                // The up-sweep folds every level but the top into scratch for the down-sweep.
                for (std::size_t i = 0; i + 1 < level_count; ++i)
                {
                    const detail::ReduceLevel& level = levels[i];
                    const bool first = (i == 0);

                    const std::uint32_t* src =
                        first ? counts_base : (up_scratch + level.in_offset);
                    std::uint32_t* dst = up_scratch + level.out_offset;

                    Reads reads = first
                        ? Reads(counts, state_region)
                        : Reads(Core::ResourceRegion{up_key,
                                                     level.in_offset * sizeof(std::uint32_t),
                                                     level.in_count * sizeof(std::uint32_t)},
                                state_region);
                    Writes writes = Writes(Core::ResourceRegion{
                        up_key, level.out_offset * sizeof(std::uint32_t),
                        level.out_count * sizeof(std::uint32_t)});

                    recorder.kernel(
                        DeviceSized{level.out_count, state_ptr + level_count + i, state_ptr + i},
                        Access(reads, writes),
                        [src, dst](std::size_t t, std::size_t in_count)
                        {
                            const std::size_t begin = t * tile;
                            std::size_t end = begin + tile;
                            if (end > in_count) end = in_count;
                            dst[t] = detail::fold_tile(
                                [](std::uint32_t a, std::uint32_t b) { return a + b; },
                                src, begin, end);
                        },
                        NodeOptions{level.out_count});
                }

                // The top level scans from a base of zero and always runs, to publish total.
                for (std::size_t i = level_count; i-- > 0;)
                {
                    const detail::ReduceLevel& level = levels[i];
                    const bool first = (i == 0);
                    const bool top = (i + 1 == level_count);

                    const std::uint32_t* src =
                        first ? counts_base : (up_scratch + level.in_offset);
                    std::uint32_t* dst =
                        first ? offsets_base : (down_scratch + level.in_offset);

                    if (top)
                    {
                        Reads reads = first
                            ? Reads(counts, state_region)
                            : Reads(Core::ResourceRegion{
                                  up_key, level.in_offset * sizeof(std::uint32_t),
                                  level.in_count * sizeof(std::uint32_t)},
                                    state_region);
                        Writes writes = first
                            ? Writes(offsets, total)
                            : Writes(Core::ResourceRegion{
                                  down_key, level.in_offset * sizeof(std::uint32_t),
                                  level.in_count * sizeof(std::uint32_t)},
                                    total);
                        recorder.kernel(
                            DeviceSized{std::size_t{1}, nullptr, state_ptr + i},
                            Access(reads, writes),
                            [src, dst, offsets_base, total_base, first](
                                std::size_t, std::size_t in_count)
                            {
                                std::uint32_t acc = 0;
                                for (std::size_t j = 0; j < in_count; ++j)
                                {
                                    dst[j] = acc;
                                    acc += src[j];
                                }
                                total_base[0] = acc;
                                if (first)
                                    offsets_base[in_count] = acc;
                            },
                            NodeOptions{std::size_t{1}});
                        continue;
                    }

                    // Each tile's base is the exclusive prefix the level above produced for it.
                    const std::uint32_t* base_arr = down_scratch + level.out_offset;

                    Reads reads = first
                        ? Reads(counts, Core::ResourceRegion{
                              down_key, level.out_offset * sizeof(std::uint32_t),
                              level.out_count * sizeof(std::uint32_t)}, state_region)
                        : Reads(Core::ResourceRegion{
                              up_key, level.in_offset * sizeof(std::uint32_t),
                              level.in_count * sizeof(std::uint32_t)},
                                Core::ResourceRegion{
                                    down_key, level.out_offset * sizeof(std::uint32_t),
                                    level.out_count * sizeof(std::uint32_t)},
                                state_region);
                    Writes writes = first
                        ? Writes(offsets)
                        : Writes(Core::ResourceRegion{
                              down_key, level.in_offset * sizeof(std::uint32_t),
                              level.in_count * sizeof(std::uint32_t)});

                    recorder.kernel(
                        DeviceSized{level.out_count, state_ptr + level_count + i, state_ptr + i},
                        Access(reads, writes),
                        [src, dst, base_arr](std::size_t t, std::size_t in_count)
                        {
                            const std::size_t begin = t * tile;
                            std::size_t end = begin + tile;
                            if (end > in_count) end = in_count;
                            std::uint32_t acc = base_arr[t];
                            for (std::size_t j = begin; j < end; ++j)
                            {
                                dst[j] = acc;
                                acc += src[j];
                            }
                        },
                        NodeOptions{level.out_count});
                }

                if (level_count == 1)
                    return;

                // The closing write copies the settled total into offsets[live n].
                Reads closing_reads = Reads(total, state_region);
                Writes closing_writes = Writes(offsets);
                recorder.kernel(
                    DeviceSized{std::size_t{1}, nullptr, state_ptr},
                    Access(closing_reads, closing_writes),
                    [offsets_base, total_base](std::size_t, std::size_t live_n)
                    {
                        offsets_base[live_n] = total_base[0];
                    },
                    NodeOptions{std::size_t{1}});
            }
        } // namespace Primitives
    } // namespace API
} // namespace SushiRuntime
