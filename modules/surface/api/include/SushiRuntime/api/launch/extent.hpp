/**************************************************************************/
/* extent.hpp                                                             */
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
 * @file extent.hpp
 * @brief Declares Extent, the N-D iteration box of a fluent kernel.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
 */

#pragma once

#include <cstddef>
#include <type_traits>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Describes the iteration shape of a fluent kernel: an N-D box with an origin.
         * @tparam N Number of dimensions (1, 2, or 3), matching SYCL's range rank.
         * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
         */
        template <int N>
        struct Extent
        {
            static_assert(N >= 1 && N <= 3, "Extent supports 1, 2, or 3 dimensions.");
            static_assert(std::is_standard_layout_v<sycl::id<N>>,
                          "origin and offset share one object, which needs a standard-layout id.");

            sycl::range<N> extent;  /**< Work-items per dimension. */

            /**
             * @brief Holds the origin under two names, origin and the deprecated offset.
             * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
             */
            union
            {
                sycl::id<N> origin = sycl::id<N>{};  /**< Index of the first work-item. */

                /** @brief Names the same value as origin. */
                [[deprecated("use origin")]] sycl::id<N> offset;
            };

            /**
             * @brief A 1-D extent from a plain element count (origin 0).
             * @param n Number of work-items to iterate.
             */
            template <int M = N, std::enable_if_t<M == 1, int> = 0>
            Extent(std::size_t n) : extent(n) {}

            /**
             * @brief An extent from explicit per-dimension sizes (origin 0).
             * @param sizes Exactly N work-item counts, one per dimension, in SYCL
             *              order (slowest-varying first).
             */
            template <class... Sizes,
                      std::enable_if_t<N >= 2 && sizeof...(Sizes) == N &&
                                       (std::is_convertible_v<Sizes, std::size_t> && ...),
                                       int> = 0>
            Extent(Sizes... sizes)
                : extent(static_cast<std::size_t>(sizes)...) {}

            /**
             * @brief An extent from a SYCL range, origin 0.
             * @param r Work-items per dimension.
             */
            explicit Extent(sycl::range<N> r) : extent(r) {}

            /**
             * @brief An extent from a SYCL range and an explicit origin.
             * @param r Work-items per dimension.
             * @param o Index of the first work-item, so the kernel iterates a sub-box.
             */
            Extent(sycl::range<N> r, sycl::id<N> o) : extent(r), origin(o) {}

            /**
             * @brief Reports how much work this shape represents.
             * @return Total work-items, the product of the per-dimension sizes.
             */
            [[nodiscard]] std::size_t count() const noexcept { return extent.size(); }

            /**
             * @brief Rebases the same box at a different origin.
             * @param first Index of the first work-item in the returned extent.
             * @return A copy with this extent's sizes and the given origin.
             */
            [[nodiscard]] Extent at(sycl::id<N> first) const noexcept
            {
                return Extent(extent, first);
            }
        };

        // The guide deduces the rank from the argument count: Extent{W, H} is Extent<2>.
        template <class... Sizes>
        Extent(Sizes...) -> Extent<static_cast<int>(sizeof...(Sizes))>;

        template <int N>
        Extent(sycl::range<N>) -> Extent<N>;

        template <int N>
        Extent(sycl::range<N>, sycl::id<N>) -> Extent<N>;

    } // namespace API

    using API::Extent;

} // namespace SushiRuntime
