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
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Returns the device of the first region of @p node whose home is device-only memory of a device other than @p device. */
            std::optional<std::size_t> device_only_elsewhere(const Node& node, const PlacementMemory& memory,
                                                             std::size_t device)
            {
                for (const std::vector<BufferRegion>* regions : {&node.io_writes_, &node.io_reads_})
                    for (const BufferRegion& region : *regions)
                    {
                        const std::optional<std::size_t> holder = memory.owning_device(region.base);
                        if (holder && *holder != device && !memory.host_visible(region.base))
                            return holder;
                    }
                return std::nullopt;
            }

            /** @brief Returns the refusal of a node fixed to its home, stated on @p stated while its memory lies on @p holder. */
            Error fixed_refusal(std::size_t stated, std::size_t holder, const char* where)
            {
                return Error{
                    ErrorCode::InvalidGraph,
                    "it is placed on device " + std::to_string(stated) + ", but its memory belongs "
                    "to device " + std::to_string(holder) + " " + where + ", and its kernel captured "
                    "raw addresses that cannot be rebound to another device's instance; place it on "
                    "device " + std::to_string(holder) + " or record it as a capture launch."};
            }
        } // namespace

        GlobalHandle anchor_of(const Node& node) noexcept
        {
            if (!node.io_writes_.empty())
                return node.io_writes_.front().base;
            if (!node.io_reads_.empty())
                return node.io_reads_.front().base;
            return GlobalHandle::null();
        }

        std::optional<std::size_t> anchor_device(const Node& node, const PlacementMemory& memory)
        {
            return memory.owning_device(anchor_of(node));
        }

        /**
         * @brief Returns the device @p node runs on, or the refusal of its stated device.
         */
        Result<std::size_t> placement_of(const Node& node,
                                         const Internal::Memory::USMAllocator& memory,
                                         const Internal::Memory::ResidencyView& handles)
        {
            return placement_of(node, USMPlacementMemory(memory, handles));
        }

        /**
         * @brief Returns the device @p node runs on, reading memory through a port.
         */
        Result<std::size_t> placement_of(const Node& node, const PlacementMemory& memory)
        {
            const std::optional<std::size_t> stated = node.stated_placement();
            const std::optional<std::size_t> owner = anchor_device(node, memory);

            if (!owner)
                return stated.value_or(K_DEFAULT_PLACEMENT);
            if (!stated || *stated == *owner)
                return *owner;

            // An index past the allocator's devices is refused by the dispatch, which knows why.
            if (*stated >= memory.device_count())
                return *stated;

            // A rebindable kernel reads its device's instance, which the planner copies there.
            if (node.kernel_addressing == KernelAddressing::Rebindable)
                return *stated;

            // A fixed kernel's raw addresses hold only in the owner's context.
            if (!memory.shares_context(*stated, *owner))
                return make_error(fixed_refusal(*stated, *owner, "in another SYCL context"));

            // Inside it, device USM of a sibling device is not addressable from the stated one.
            if (const std::optional<std::size_t> holder = device_only_elsewhere(node, memory, *stated))
                return make_error(fixed_refusal(*stated, *holder, "as device USM"));
            return *stated;
        }

    } // namespace Graph
} // namespace SushiRuntime
