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
 * @brief Declares SplitDispatchPart and dispatch_split(), which run a split node's parts.
 * @see modules/engine/task/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <functional>

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
         * @brief Runs @p split over @p parts, each on its device, then joins them.
         *
         * Each slice receives its part, the device's queue and the pointers bound on that device;
         * the combine then receives the parts in slice order. Binding takes no lock.
         * @param pointers The node's kernel pointers, or null for a node whose kernel takes none.
         * @param queue_of Returns the queue of a device index.
         */
        SUSHIRUNTIME_API void dispatch_split(const NodeSplit& split, span<const SplitDispatchPart> parts,
                                             KernelPointers* pointers,
                                             const std::function<sycl::queue&(std::size_t)>& queue_of);
    } // namespace Internal::Graph
} // namespace SushiRuntime
