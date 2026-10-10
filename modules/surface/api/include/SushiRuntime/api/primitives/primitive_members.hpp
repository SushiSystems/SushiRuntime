/**************************************************************************/
/* primitive_members.hpp                                                  */
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
 * @file primitive_members.hpp
 * @brief Declares PrimitiveMembers, the add_reduce() family a graph and a region forward from.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/primitives/counting_sort.hpp>
#include <SushiRuntime/api/primitives/prefix_scan.hpp>
#include <SushiRuntime/api/primitives/reduce.hpp>
#include <SushiRuntime/api/primitives/segmented_reduce.hpp>
#include <SushiRuntime/api/vocabulary/late_count.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace Primitives
        {
            /**
             * @brief Gives a type that owns a Recorder the numeric add_*() members.
             * @tparam Derived The builder; it has a public recorder() that returns its Recorder.
             * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
             */
            template <class Derived>
            class PrimitiveMembers
            {
                public:
                    /**
                     * @brief Adds a reduction whose combination order never varies.
                     * @tparam Combine Binary functor; must be trivially copyable.
                     * @param input Values to reduce; must hold at least n elements.
                     * @throws Core::SushiException If @p n exceeds @p input's size, if
                     *        @p output is empty, or if scratch cannot be allocated.
                     * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
                     */
                    template <typename T, class Combine>
                    Derived& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                      std::size_t n, Combine op, T identity)
                    {
                        reduce(self().recorder(), input, output, n, op, identity);
                        return self();
                    }

                    /**
                     * @brief Adds a fixed-order reduction using the combiner's static identity().
                     * @throws Core::SushiException As the five-argument overload.
                     * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
                     */
                    template <typename T, class Combine>
                    Derived& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                      std::size_t n, Combine op)
                    {
                        reduce(self().recorder(), input, output, n, op);
                        return self();
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
                    Derived& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                      LateCount n, Combine op, T identity)
                    {
                        reduce(self().recorder(), input, output, n, op, identity);
                        return self();
                    }

                    /**
                     * @brief Adds a late-bound reduction using the combiner's static identity().
                     * @param input Values to reduce; must hold at least n.capacity elements.
                     * @throws Core::SushiException As the five-argument overload.
                     */
                    template <typename T, class Combine>
                    Derived& add_reduce(const Buffer<T>& input, const Buffer<T>& output,
                                      LateCount n, Combine op)
                    {
                        reduce(self().recorder(), input, output, n, op);
                        return self();
                    }

                    /**
                     * @brief Reduces each segment of a ragged array in a fixed order.
                     * @param segment_offsets segment_count + 1 non-decreasing entries, the last in
                     *        values.
                     * @throws Core::SushiException If segment_offsets or output is too small for
                     *         segment_count.
                     * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
                     */
                    template <typename T, class Combine>
                    Derived& add_segmented_reduce(const Buffer<T>& values,
                                                const Buffer<std::uint32_t>& segment_offsets,
                                                const Buffer<T>& output,
                                                std::size_t segment_count,
                                                Combine op, T identity)
                    {
                        segmented_reduce(self().recorder(), values, segment_offsets, output,
                                         segment_count, op, identity);
                        return self();
                    }

                    /**
                     * @brief Reduces each segment using the combiner's static identity().
                     * @param segment_offsets segment_count + 1 non-decreasing entries.
                     * @param output Holds segment_count entries.
                     * @throws Core::SushiException As the six-argument overload.
                     */
                    template <typename T, class Combine>
                    Derived& add_segmented_reduce(const Buffer<T>& values,
                                                const Buffer<std::uint32_t>& segment_offsets,
                                                const Buffer<T>& output,
                                                std::size_t segment_count, Combine op)
                    {
                        segmented_reduce(self().recorder(), values, segment_offsets, output,
                                         segment_count, op);
                        return self();
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
                    Derived& add_segmented_reduce(const Buffer<T>& values,
                                                const Buffer<std::uint32_t>& segment_offsets,
                                                const Buffer<T>& output,
                                                LateCount segment_count,
                                                Combine op, T identity)
                    {
                        segmented_reduce(self().recorder(), values, segment_offsets, output,
                                         segment_count, op, identity);
                        return self();
                    }

                    /**
                     * @brief Reduces each segment, late-bound, with the combiner's identity().
                     * @param segment_offsets segment_count.capacity + 1 non-decreasing entries.
                     * @param output Holds segment_count.capacity entries.
                     * @throws Core::SushiException As the late-bound six-argument overload.
                     */
                    template <typename T, class Combine>
                    Derived& add_segmented_reduce(const Buffer<T>& values,
                                                const Buffer<std::uint32_t>& segment_offsets,
                                                const Buffer<T>& output,
                                                LateCount segment_count, Combine op)
                    {
                        segmented_reduce(self().recorder(), values, segment_offsets, output,
                                         segment_count, op);
                        return self();
                    }

                    /**
                     * @brief Scans uint32 counts into exclusive running offsets, late-bound.
                     * @param offsets Destination; must hold at least n.capacity + 1 elements.
                     * @throws Core::SushiException If a buffer is too small, if @p counts
                     *        and @p offsets sit on different devices, or at run if the
                     *        live count exceeds @p n.capacity.
                     * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
                     */
                    Derived& add_prefix_scan(const Buffer<std::uint32_t>& counts,
                                           const Buffer<std::uint32_t>& offsets,
                                           const Buffer<std::uint32_t>& total,
                                           LateCount n)
                    {
                        prefix_scan(self().recorder(), counts, offsets, total, n);
                        return self();
                    }

                    /**
                     * @brief Stably argsorts keys by 8-bit digits into bucket offsets.
                     * @throws Core::SushiException If a buffer is too small, if @p keys,
                     *        @p indices, @p offsets or @p counts sit on different
                     *        devices, if @p key_range.capacity is zero, or at run if
                     *        either live count exceeds its capacity.
                     * @see modules/surface/api/include/SushiRuntime/api/primitives/README.md
                     */
                    Derived& add_counting_sort(const Buffer<std::uint32_t>& keys,
                                             const Buffer<std::uint32_t>& indices,
                                             const Buffer<std::uint32_t>& offsets,
                                             const Buffer<std::uint32_t>& total,
                                             LateCount n, LateCount key_range,
                                             const Buffer<std::uint32_t>* counts = nullptr)
                    {
                        counting_sort(self().recorder(), keys, indices, offsets, total, n,
                                      key_range, counts);
                        return self();
                    }

                protected:
                    PrimitiveMembers() = default;
                    PrimitiveMembers(const PrimitiveMembers&) = default;
                    PrimitiveMembers(PrimitiveMembers&&) = default;
                    PrimitiveMembers& operator=(const PrimitiveMembers&) = default;
                    PrimitiveMembers& operator=(PrimitiveMembers&&) = default;
                    ~PrimitiveMembers() = default;

                private:
                    /** @brief Returns the builder these members belong to. */
                    Derived& self() noexcept { return static_cast<Derived&>(*this); }
            };
        } // namespace Primitives
    } // namespace API
} // namespace SushiRuntime
