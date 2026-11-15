/**************************************************************************/
/* split_dispatch.cpp                                                     */
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
 * @file split_dispatch.cpp
 * @brief Defines dispatch_split().
 * @see modules/engine/task/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/split_dispatch.hpp>

#include <vector>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        void dispatch_split(const NodeSplit& split, span<const SplitDispatchPart> parts, KernelPointers* pointers,
                            const std::function<sycl::queue&(std::size_t)>& queue_of)
        {
            std::vector<SplitPart> order;
            order.reserve(parts.size());
            for (const SplitDispatchPart& entry : parts)
            {
                span<void* const> bound;
                if (pointers)
                {
                    pointers->bind(entry.device);
                    bound = span<void* const>(pointers->table(), pointers->size());
                }
                if (split.slice)
                    split.slice(entry.part, queue_of(entry.device), bound);
                order.push_back(entry.part);
            }
            if (split.combine)
                split.combine(span<const SplitPart>(order.data(), order.size()));
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
