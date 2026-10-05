/****************************************************************/
/* extent.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>
#include <type_traits>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief The iteration shape of a fluent kernel: an N-D box with an origin.
         *
         * Extent is the launch vocabulary the multidimensional add() overloads key
         * on. It pairs a global @ref extent (how many work-items per dimension) with
         * an @ref offset origin, so a kernel can iterate the whole field or just a
         * sub-box of it — the latter is what makes interior-only stencils and
         * halo/ghost-layer updates expressible without a hand-rolled handler.
         *
         * A 1-D Extent is implicitly constructible from a plain element count, so
         * every existing add(std::size_t, fn) call site keeps compiling unchanged
         * and 1-D stays the zero-ceremony default.
         *
         * @tparam N Number of dimensions (1, 2, or 3), matching SYCL's range rank.
         */
        template <int N>
        struct Extent
        {
            static_assert(N >= 1 && N <= 3, "Extent supports 1, 2, or 3 dimensions.");

            sycl::range<N> extent;                  /**< Work-items per dimension. */
            sycl::id<N>    offset = sycl::id<N>{};   /**< Origin of the iterated box. */

            /**
             * @brief A 1-D extent from a plain element count (offset 0).
             * @param n Number of work-items to iterate.
             */
            template <int M = N, std::enable_if_t<M == 1, int> = 0>
            Extent(std::size_t n) : extent(n) {}

            /**
             * @brief An extent from explicit per-dimension sizes (offset 0).
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
             * @brief An extent from a SYCL range, offset 0.
             * @param r Work-items per dimension.
             */
            explicit Extent(sycl::range<N> r) : extent(r) {}

            /**
             * @brief An extent from a SYCL range and an explicit origin.
             * @param r Work-items per dimension.
             * @param o Index of the first work-item, so the kernel iterates a sub-box.
             */
            Extent(sycl::range<N> r, sycl::id<N> o) : extent(r), offset(o) {}

            /**
             * @brief Reports how much work this shape represents.
             * @return Total work-items, the product of the per-dimension sizes.
             */
            std::size_t count() const noexcept { return extent.size(); }

            /**
             * @brief Rebases the same box at a different origin.
             * @param origin Index of the first work-item in the returned extent.
             * @return A copy with this extent's sizes and the given origin.
             */
            Extent at(sycl::id<N> origin) const noexcept
            {
                return Extent(extent, origin);
            }
        };

        // Deduction so `Extent{W, H}` yields Extent<2>, `Extent{W, H, D}` -> <3>,
        // and `Extent{N}` -> <1>, without spelling the rank.
        template <class... Sizes>
        Extent(Sizes...) -> Extent<static_cast<int>(sizeof...(Sizes))>;

        template <int N>
        Extent(sycl::range<N>) -> Extent<N>;

        template <int N>
        Extent(sycl::range<N>, sycl::id<N>) -> Extent<N>;

    } // namespace API

    using API::Extent;

} // namespace SushiRuntime
