/**************************************************************************/
/* node_placement.cpp                                                     */
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
 * @file node_placement.cpp
 * @brief Names the device a graph node runs on from its regions and its stated placement.
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/node_placement.hpp>

#include <cstddef>
#include <optional>
#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Returns the base of the first write region, else of the first read region. */
            void* anchor_of(const Node& node) noexcept
            {
                if (!node.io_writes_.empty())
                    return node.io_writes_.front().base;
                if (!node.io_reads_.empty())
                    return node.io_reads_.front().base;
                return nullptr;
            }
        } // namespace

        /**
         * @brief Returns the device @p node runs on, or the refusal of its stated device.
         */
        Result<std::size_t> placement_of(const Node& node,
                                         const Internal::Memory::USMAllocator& memory)
        {
            return placement_of(node, USMPlacementMemory(memory));
        }

        /**
         * @brief Returns the device @p node runs on, reading memory through a port.
         */
        Result<std::size_t> placement_of(const Node& node, const PlacementMemory& memory)
        {
            const std::optional<std::size_t> stated = node.stated_placement();
            const std::optional<std::size_t> owner = memory.owning_device(anchor_of(node));

            if (!owner)
                return stated.value_or(K_DEFAULT_PLACEMENT);
            if (!stated || *stated == *owner)
                return *owner;

            // An index past the allocator's devices is refused by the dispatch, which knows why.
            if (*stated >= memory.device_count() || memory.shares_context(*stated, *owner))
                return *stated;

            return make_error(Error{
                ErrorCode::InvalidGraph,
                "it is placed on device " + std::to_string(*stated) + ", but its memory belongs "
                "to device " + std::to_string(*owner) + " in another SYCL context; place it on "
                "a device of that context."});
        }

    } // namespace Graph
} // namespace SushiRuntime
