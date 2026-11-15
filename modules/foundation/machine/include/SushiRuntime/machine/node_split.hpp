/**************************************************************************/
/* node_split.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file node_split.hpp
 * @brief Declares NodeSplit, how a node states that it may run in parts.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

#include <cstdint>
#include <functional>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/machine/split_part.hpp>

namespace SushiRuntime
{
    /**
     * @brief States that a node may be cut along one axis into parts the runtime places.
     *
     * The runtime picks how many parts there are and where each runs; the consumer owns the
     * kernels. The slice runs once per part on the part's device; the combine, when given, joins
     * the parts in slice order. A node states it through NodeOptions::split.
     * @see modules/foundation/machine/README.md
     */
    struct NodeSplit
    {
        std::uint32_t axis = 0;   /**< The axis to cut along, an index the consumer defines. */
        std::uint64_t extent = 0; /**< Length of that axis; the parts cover [0, extent). */

        /**
         * @brief Submits the node's work over one part to the queue of the part's device.
         *
         * The pointers are the node's kernel pointers bound on that device, in the order the node
         * recorded them; they stay valid for the call only, so a kernel copies the ones it reads.
         */
        std::function<void(const SplitPart&, sycl::queue&, span<void* const>)> slice;

        /** @brief Joins the parts, given in slice order; empty when the parts need no join. */
        std::function<void(span<const SplitPart>)> combine;
    };
} // namespace SushiRuntime
