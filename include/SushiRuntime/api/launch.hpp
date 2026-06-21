/**************************************************************************/
/* launch.hpp                                                             */
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

#include <array>
#include <cstddef>

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

            /** @brief A 1-D extent from a plain element count (offset 0). */
            template <int M = N, std::enable_if_t<M == 1, int> = 0>
            Extent(std::size_t n) : extent(n) {}

            /** @brief An extent from explicit per-dimension sizes (offset 0). */
            template <class... Sizes,
                      std::enable_if_t<N >= 2 && sizeof...(Sizes) == N &&
                                       (std::is_convertible_v<Sizes, std::size_t> && ...),
                                       int> = 0>
            Extent(Sizes... sizes)
                : extent(static_cast<std::size_t>(sizes)...) {}

            /** @brief An extent from a SYCL range, offset 0. */
            explicit Extent(sycl::range<N> r) : extent(r) {}

            /** @brief An extent from a SYCL range and an explicit origin. */
            Extent(sycl::range<N> r, sycl::id<N> o) : extent(r), offset(o) {}

            /** @brief Total number of work-items (product of the per-dim sizes). */
            std::size_t count() const noexcept { return extent.size(); }

            /** @brief Returns a copy iterating the sub-box at @p origin. */
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

        /**
         * @brief An nd_range launch: a global iteration box tiled by a local size.
         *
         * NdRange unlocks the work-group execution model — barriers, local (shared)
         * memory, and sub-group operations — for callers who need tiled kernels
         * (e.g. blocked matrix multiply, shared-memory stencils). The fluent
         * overload that accepts one hands the kernel a sycl::nd_item<N>, exactly as
         * a raw SYCL nd_range parallel_for would.
         *
         * Each global dimension must be a whole multiple of the matching local
         * dimension, per the SYCL requirement; the runtime does not pad.
         *
         * @tparam N Number of dimensions (1, 2, or 3).
         */
        template <int N>
        struct NdRange
        {
            static_assert(N >= 1 && N <= 3, "NdRange supports 1, 2, or 3 dimensions.");

            sycl::range<N> global;  /**< Total work-items per dimension. */
            sycl::range<N> local;   /**< Work-group size per dimension. */

            /** @brief Builds the launch from global and local ranges. */
            NdRange(sycl::range<N> g, sycl::range<N> l) : global(g), local(l) {}

            /** @brief The SYCL nd_range this launch describes. */
            sycl::nd_range<N> to_nd_range() const noexcept
            {
                return sycl::nd_range<N>(global, local);
            }

            /** @brief Total number of work-items (product of the global sizes). */
            std::size_t count() const noexcept { return global.size(); }
        };

        template <int N>
        NdRange(sycl::range<N>, sycl::range<N>) -> NdRange<N>;

    } // namespace API

    using API::Extent;
    using API::NdRange;

} // namespace SushiRuntime
