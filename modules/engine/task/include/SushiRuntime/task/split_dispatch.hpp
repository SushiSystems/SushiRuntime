/**************************************************************************/
/* split_dispatch.hpp                                                     */
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
 * @file split_dispatch.hpp
 * @brief Declares SplitDispatchPart and dispatch_split() and join_split(), which run a split node's parts.
 * @see modules/engine/task/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/machine/node_split.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /** @brief Names one part of a split node and the device index it runs on. */
        struct SplitDispatchPart
        {
            std::size_t device = 0; /**< Index into the runtime's device set. */
            SplitPart part;         /**< Range of the axis and place in slice order. */
        };

        /**
         * @brief Submits every part of @p split, each on its device, and returns their events in slice order.
         *
         * Each slice gets its part, the device's queue, @p dependencies and @p pointers bound there
         * (null for a node with no pointers); nothing is waited on, so all parts are in flight at once.
         * @param dependencies The node's input events, the only ones each part depends on.
         * @param queue_of Returns the queue of a device index.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<sycl::event>
        dispatch_split(const NodeSplit& split, span<const SplitDispatchPart> parts, span<const sycl::event> dependencies,
                       KernelPointers* pointers, const std::function<sycl::queue&(std::size_t)>& queue_of);

        /**
         * @brief Waits on @p part_events, then hands the parts to the split's combine in slice order.
         *
         * Only these events are waited on, never a whole queue, so other work on a part's queue
         * does not hold the node. The wait also drives a lazy event model such as AdaptiveCpp's.
         * @param part_events The events dispatch_split() returned for @p parts.
         */
        SUSHIRUNTIME_API void join_split(const NodeSplit& split, span<const SplitDispatchPart> parts,
                                         span<const sycl::event> part_events);
    } // namespace Internal::Graph
} // namespace SushiRuntime
