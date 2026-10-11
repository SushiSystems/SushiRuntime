/**************************************************************************/
/* nd_range.hpp                                                           */
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
 * @file nd_range.hpp
 * @brief Declares NdRange, a global iteration box tiled by a local size.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
 */

#pragma once

#include <cstddef>

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    /**
     * @brief Describes an nd_range launch: a global iteration box tiled by a local size.
     * @tparam N Number of dimensions (1, 2, or 3).
     * @pre Each global dimension is a whole multiple of the matching local dimension.
     * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
     */
    template <int N>
    struct NdRange
    {
        static_assert(N >= 1 && N <= 3, "NdRange supports 1, 2, or 3 dimensions.");

        sycl::range<N> global;  /**< Total work-items per dimension. */
        sycl::range<N> local;   /**< Work-group size per dimension. */
        sycl::id<N> origin = sycl::id<N>{};  /**< Index the first work-item reports. */

        /**
         * @brief Builds the launch from global and local ranges, origin 0.
         * @param g Total work-items per dimension.
         * @param l Work-group size per dimension; each @p g dimension must be a
         *          whole multiple of the matching @p l dimension.
         */
        NdRange(sycl::range<N> g, sycl::range<N> l) : global(g), local(l) {}

        /**
         * @brief Builds the launch from global and local ranges and an origin.
         * @param g Total work-items per dimension; a whole multiple of @p l per dimension.
         * @param l Work-group size per dimension.
         * @param o Added to each work-item's global id before the kernel receives it.
         */
        NdRange(sycl::range<N> g, sycl::range<N> l, sycl::id<N> o)
            : global(g), local(l), origin(o) {}

        /**
         * @brief Converts to the SYCL type a parallel_for takes.
         * @return An nd_range carrying this launch's global and local ranges; the origin is
         *         applied by the launch, not by SYCL.
         */
        [[nodiscard]] sycl::nd_range<N> to_nd_range() const noexcept
        {
            return sycl::nd_range<N>(global, local);
        }

        /**
         * @brief Reports how much work this launch represents.
         * @return Total work-items, the product of the global sizes.
         */
        [[nodiscard]] std::size_t count() const noexcept { return global.size(); }

        /**
         * @brief Rebases the same tiling at a different origin.
         * @param first Index the first work-item reports in the returned launch.
         * @return A copy with this launch's ranges and the given origin.
         * @see modules/surface/api/include/SushiRuntime/api/launch/README.md
         */
        [[nodiscard]] NdRange at(sycl::id<N> first) const noexcept
        {
            return NdRange(global, local, first);
        }
    };

    template <int N>
    NdRange(sycl::range<N>, sycl::range<N>) -> NdRange<N>;

    template <int N>
    NdRange(sycl::range<N>, sycl::range<N>, sycl::id<N>) -> NdRange<N>;



} // namespace SushiRuntime
