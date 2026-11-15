/**************************************************************************/
/* usm_placement_memory.cpp                                               */
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
 * @file usm_placement_memory.cpp
 * @brief Defines USMPlacementMemory.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#include <SushiRuntime/task/usm_placement_memory.hpp>

#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        USMPlacementMemory::USMPlacementMemory(const Internal::Memory::USMAllocator& memory,
                                               const Internal::Memory::ResidencyView& handles) noexcept
            : memory_(memory), handles_(handles)
        {
        }

        std::optional<std::size_t> USMPlacementMemory::recorded_device(GlobalHandle handle) const
        {
            const std::optional<HandleLocation> location = locate(handles_, handle, memory_.device_count());
            if (!location)
                return std::nullopt;
            return location->device;
        }

        std::optional<std::size_t> USMPlacementMemory::owning_device(GlobalHandle handle) const
        {
            return recorded_device(handle);
        }

        std::size_t USMPlacementMemory::device_count() const
        {
            return memory_.device_count();
        }

        bool USMPlacementMemory::shares_context(std::size_t first, std::size_t second) const
        {
            return memory_.shares_context(first, second);
        }

        bool USMPlacementMemory::host_visible(GlobalHandle handle) const
        {
            return home_is_host_visible(handles_.inspect(handle), handle);
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
