/**************************************************************************/
/* segmented_reduce.hpp                                                   */
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
 * @file segmented_reduce.hpp
 * @brief Declares segmented_reduce(), a fixed-order reduction of each segment of a ragged array.
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
             * @brief Reduces each segment of a ragged array in a fixed order.
             * @param segment_offsets segment_count + 1 non-decreasing entries, the last within
             *        values.
             * @throws Core::SushiException If segment_offsets or output is too small for
             *         segment_count.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <typename T, class Combine>
            void segmented_reduce(Recorder& recorder,
                                  const Buffer<T>& values,
                                  const Buffer<std::uint32_t>& segment_offsets,
                                  const Buffer<T>& output,
                                  std::size_t segment_count,
                                  Combine op, T identity)
            {
                if (segment_offsets.size() < segment_count + 1)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_segmented_reduce: segment_offsets holds " +
                        std::to_string(segment_offsets.size()) + " entries but " +
                        std::to_string(segment_count + 1) + " are required."});
                if (output.size() < segment_count)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_segmented_reduce: output holds " +
                        std::to_string(output.size()) + " entries but " +
                        std::to_string(segment_count) + " are required."});

                const T* value_base = values.data();
                const std::uint32_t* offset_base = segment_offsets.data();
                T* out_base = output.data();

                const Reads reads(values, segment_offsets);
                const Writes writes(output);
                recorder.kernel(segment_count, Access(reads, writes),
                    [value_base, offset_base, out_base, op, identity](std::size_t s)
                    {
                        const std::size_t begin = offset_base[s];
                        const std::size_t end = offset_base[s + 1];
                        out_base[s] = (end > begin)
                            ? detail::fold_tile(op, value_base, begin, end)
                            : identity;
                    },
                    NodeOptions{segment_count});
            }

            /**
             * @brief Reduces each segment with a late-bound segment count.
             * @param segment_offsets segment_count.capacity + 1 non-decreasing entries.
             * @param output Holds segment_count.capacity entries.
             * @throws Core::SushiException If segment_offsets or output is too small for
             *        segment_count.capacity, or at run if the live count exceeds it.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <typename T, class Combine>
            void segmented_reduce(Recorder& recorder,
                                  const Buffer<T>& values,
                                  const Buffer<std::uint32_t>& segment_offsets,
                                  const Buffer<T>& output,
                                  LateCount segment_count,
                                  Combine op, T identity)
            {
                if (segment_offsets.size() < segment_count.capacity + 1)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_segmented_reduce: segment_offsets holds " +
                        std::to_string(segment_offsets.size()) + " entries but " +
                        std::to_string(segment_count.capacity + 1) + " are required."});
                if (output.size() < segment_count.capacity)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "add_segmented_reduce: output holds " +
                        std::to_string(output.size()) + " entries but " +
                        std::to_string(segment_count.capacity) + " are required."});

                detail::validate_late_count(segment_count, "add_segmented_reduce");

                const T* value_base = values.data();
                const std::uint32_t* offset_base = segment_offsets.data();
                T* out_base = output.data();
                const LateCount lc = segment_count;

                // Same shape as the late-bound add_reduce, one live word instead of a level chain.
                std::uint32_t* state = recorder.scratch<std::uint32_t>(
                    1, values.device_index(), values.residency());
                std::uint32_t* state_ptr = state;
                const Core::ResourceRegion state_region{state_ptr, 0, sizeof(std::uint32_t)};

                auto rule = recorder.rule();

                recorder.resolve(std::array<LateCount, 1>{lc}, values.device_index(),
                    Access(Reads(), Writes(state_region)),
                    [state_ptr](const std::array<std::size_t, 1>& live)
                    {
                        state_ptr[0] = static_cast<std::uint32_t>(live[0]);
                    },
                    NodeOptions{std::size_t{1}});

                Reads reads = Reads(values, segment_offsets, state_region);
                Writes writes = Writes(output);
                recorder.kernel(
                    DeviceSized{segment_count.capacity, state_ptr, nullptr},
                    Access(reads, writes),
                    [value_base, offset_base, out_base, op, identity](std::size_t s)
                    {
                        const std::size_t begin = offset_base[s];
                        const std::size_t end = offset_base[s + 1];
                        out_base[s] = (end > begin)
                            ? detail::fold_tile(op, value_base, begin, end)
                            : identity;
                    },
                    NodeOptions{segment_count.capacity});
            }

            /**
             * @brief Reduces each segment using the combiner's static identity().
             * @param segment_offsets segment_count + 1 non-decreasing entries.
             * @param output Holds segment_count entries.
             * @throws Core::SushiException As the six-argument overload.
             */
            template <typename T, class Combine>
            void segmented_reduce(Recorder& recorder,
                                  const Buffer<T>& values,
                                  const Buffer<std::uint32_t>& segment_offsets,
                                  const Buffer<T>& output,
                                  std::size_t segment_count, Combine op)
            {
                segmented_reduce(recorder, values, segment_offsets, output,
                                 segment_count, op, Combine::identity());
            }

            /**
             * @brief Reduces each segment, late-bound, using the combiner's static identity().
             * @param segment_offsets segment_count.capacity + 1 non-decreasing entries.
             * @param output Holds segment_count.capacity entries.
             * @throws Core::SushiException As the late-bound six-argument overload.
             */
            template <typename T, class Combine>
            void segmented_reduce(Recorder& recorder,
                                  const Buffer<T>& values,
                                  const Buffer<std::uint32_t>& segment_offsets,
                                  const Buffer<T>& output,
                                  LateCount segment_count, Combine op)
            {
                segmented_reduce(recorder, values, segment_offsets, output,
                                 segment_count, op, Combine::identity());
            }
        } // namespace Primitives
    } // namespace API
} // namespace SushiRuntime
