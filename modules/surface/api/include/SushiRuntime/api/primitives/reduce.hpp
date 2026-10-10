/**************************************************************************/
/* reduce.hpp                                                             */
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
 * @file reduce.hpp
 * @brief Declares reduce(), a fixed-order reduction recorded through a Recorder.
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
             * @brief Adds a reduction whose combination order never varies.
             * @tparam Combine Binary functor; must be trivially copyable.
             * @param input Values to reduce; must hold at least n elements.
             * @throws Core::SushiException If @p n exceeds @p input's size, if
             *        @p output is empty, or if scratch cannot be allocated.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <typename T, class Combine>
            void reduce(Recorder& recorder,
                        const Buffer<T>& input, const Buffer<T>& output,
                        std::size_t n, Combine op, T identity)
            {
                if (n > input.size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_reduce: n (" + std::to_string(n) +
                        ") exceeds the input buffer's size (" +
                        std::to_string(input.size()) + ")."});
                if (output.size() == 0)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_reduce: the output buffer holds no elements."});

                T* out_base = output.data();
                if (n == 0)
                {
                    // An empty reduction still emits a node, so readers order after it.
                    const Reads reads;
                    const Writes writes(output);
                    recorder.kernel(std::size_t{1}, Access(reads, writes),
                        [out_base, identity](std::size_t)
                        {
                            out_base[0] = identity;
                        },
                        NodeOptions{std::size_t{1}});
                    return;
                }

                const std::vector<detail::ReduceLevel> levels = detail::plan_reduction(n);
                const std::size_t scratch_count = detail::reduce_scratch_count(levels);

                T* scratch = nullptr;
                if (scratch_count != 0)
                {
                    T* owned = recorder.scratch<T>(
                        scratch_count, input.device_index(), input.residency());
                    scratch = owned;
                }

                const T* in_base = input.data();
                void* scratch_key = static_cast<void*>(scratch);
                constexpr std::size_t tile = detail::REDUCE_TILE;

                // Every node this call records belongs to one rule.
                auto rule = recorder.rule();

                for (std::size_t i = 0; i < levels.size(); ++i)
                {
                    const detail::ReduceLevel& level = levels[i];
                    const bool first = (i == 0);
                    const bool last  = (i + 1 == levels.size());

                    const T* src = first ? in_base : (scratch + level.in_offset);
                    T* dst = last ? out_base : (scratch + level.out_offset);
                    const std::size_t in_count = level.in_count;

                    const Reads reads = first
                        ? Reads(input)
                        : Reads(Core::ResourceRegion{scratch_key,
                                                     level.in_offset * sizeof(T),
                                                     level.in_count * sizeof(T)});
                    const Writes writes = last
                        ? Writes(output)
                        : Writes(Core::ResourceRegion{scratch_key,
                                                      level.out_offset * sizeof(T),
                                                      level.out_count * sizeof(T)});

                    recorder.kernel(level.out_count, Access(reads, writes),
                        [src, dst, in_count, op](std::size_t t)
                        {
                            const std::size_t begin = t * tile;
                            std::size_t end = begin + tile;
                            if (end > in_count) end = in_count;
                            dst[t] = detail::fold_tile(op, src, begin, end);
                        },
                        NodeOptions{level.out_count});
                }
            }

            /**
             * @brief Adds a reduction whose live element count is late-bound.
             * @param input Values to reduce; must hold at least n.capacity elements.
             * @throws Core::SushiException If @p n.capacity exceeds @p input's
             *        size, if @p output is empty, or at run if the live count
             *        exceeds @p n.capacity.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <typename T, class Combine>
            void reduce(Recorder& recorder,
                        const Buffer<T>& input, const Buffer<T>& output,
                        LateCount n, Combine op, T identity)
            {
                detail::validate_late_count(n, "add_reduce");
                if (n.capacity > input.size())
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_reduce: n.capacity (" + std::to_string(n.capacity) +
                        ") exceeds the input buffer's size (" +
                        std::to_string(input.size()) + ")."});
                if (output.size() == 0)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_reduce: the output buffer holds no elements."});

                const std::vector<detail::ReduceLevel> levels =
                    detail::plan_tiled_levels(n.capacity);
                T* out_base = output.data();

                if (levels.empty())
                {
                    // With capacity zero the identity is the answer on every run.
                    recorder.kernel(std::size_t{1}, Access(Reads(), Writes(output)),
                        [out_base, identity](std::size_t)
                        {
                            out_base[0] = identity;
                        });
                    return;
                }

                const std::size_t scratch_count = detail::reduce_scratch_count(levels);
                T* scratch = nullptr;
                if (scratch_count != 0)
                {
                    T* owned = recorder.scratch<T>(
                        scratch_count, input.device_index(), input.residency());
                    scratch = owned;
                }

                // state[i] is level i's live in_count, state[level_count+i] its live out_count.
                const std::size_t level_count = levels.size();
                std::uint32_t* state = recorder.scratch<std::uint32_t>(
                    2 * level_count, input.device_index(), input.residency());
                std::uint32_t* state_ptr = state;

                // Every node this call records belongs to one rule.
                auto rule = recorder.rule();

                const Core::ResourceRegion state_region{state_ptr, 0,
                                                         2 * level_count * sizeof(std::uint32_t)};

                recorder.resolve(std::array<LateCount, 1>{n}, input.device_index(),
                    Access(Reads(), Writes(state_region)),
                    [state_ptr, level_count](const std::array<std::size_t, 1>& live)
                    {
                        detail::fill_live_level_words(level_count, live[0], state_ptr);
                    },
                    NodeOptions{std::size_t{1}});

                const T* in_base = input.data();
                void* scratch_key = static_cast<void*>(scratch);
                constexpr std::size_t tile = detail::REDUCE_TILE;

                for (std::size_t i = 0; i < levels.size(); ++i)
                {
                    const detail::ReduceLevel& level = levels[i];
                    const bool first = (i == 0);
                    const bool last  = (i + 1 == levels.size());

                    const T* src = first ? in_base : (scratch + level.in_offset);
                    T* dst = last ? out_base : (scratch + level.out_offset);

                    Reads reads = first
                        ? Reads(input, state_region)
                        : Reads(Core::ResourceRegion{scratch_key,
                                                     level.in_offset * sizeof(T),
                                                     level.in_count * sizeof(T)},
                                state_region);
                    Writes writes = last
                        ? Writes(output)
                        : Writes(Core::ResourceRegion{scratch_key,
                                                      level.out_offset * sizeof(T),
                                                      level.out_count * sizeof(T)});

                    recorder.kernel(
                        DeviceSized{level.out_count, state_ptr + level_count + i, state_ptr + i},
                        Access(reads, writes),
                        [src, dst, op](std::size_t t, std::size_t in_count)
                        {
                            const std::size_t begin = t * tile;
                            std::size_t end = begin + tile;
                            if (end > in_count) end = in_count;
                            dst[t] = detail::fold_tile(op, src, begin, end);
                        },
                        NodeOptions{level.out_count});
                }

                // A live count of zero folds no level, so this is the identity's only writer.
                Reads zero_reads = Reads(state_region);
                Writes zero_writes = Writes(output);
                recorder.kernel(
                    DeviceSized{std::size_t{1}, nullptr, state_ptr},
                    Access(zero_reads, zero_writes),
                    [out_base, identity](std::size_t, std::size_t live_n)
                    {
                        if (live_n == 0)
                            out_base[0] = identity;
                    },
                    NodeOptions{std::size_t{1}});
            }

            /**
             * @brief Adds a fixed-order reduction using the combiner's static identity().
             * @throws Core::SushiException As the five-argument overload.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <typename T, class Combine>
            void reduce(Recorder& recorder,
                        const Buffer<T>& input, const Buffer<T>& output,
                        std::size_t n, Combine op)
            {
                reduce(recorder, input, output, n, op, Combine::identity());
            }

            /**
             * @brief Adds a late-bound reduction using the combiner's static identity().
             * @param input Values to reduce; must hold at least n.capacity elements.
             * @throws Core::SushiException As the five-argument overload.
             */
            template <typename T, class Combine>
            void reduce(Recorder& recorder,
                        const Buffer<T>& input, const Buffer<T>& output,
                        LateCount n, Combine op)
            {
                reduce(recorder, input, output, n, op, Combine::identity());
            }
        } // namespace Primitives
    } // namespace API
} // namespace SushiRuntime
