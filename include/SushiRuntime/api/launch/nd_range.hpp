/**************************************************************************/
/* nd_range.hpp                                                              */
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

#include <sycl/sycl.hpp>

namespace SushiRuntime
{
    namespace API
    {
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

            /**
             * @brief Builds the launch from global and local ranges.
             * @param g Total work-items per dimension.
             * @param l Work-group size per dimension; each @p g dimension must be a
             *          whole multiple of the matching @p l dimension.
             */
            NdRange(sycl::range<N> g, sycl::range<N> l) : global(g), local(l) {}

            /**
             * @brief Converts to the SYCL type a parallel_for takes.
             * @return An nd_range carrying this launch's global and local ranges.
             */
            sycl::nd_range<N> to_nd_range() const noexcept
            {
                return sycl::nd_range<N>(global, local);
            }

            /**
             * @brief Reports how much work this launch represents.
             * @return Total work-items, the product of the global sizes.
             */
            std::size_t count() const noexcept { return global.size(); }
        };

        template <int N>
        NdRange(sycl::range<N>, sycl::range<N>) -> NdRange<N>;

    } // namespace API

    using API::NdRange;

} // namespace SushiRuntime
