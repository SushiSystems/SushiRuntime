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

#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        USMPlacementMemory::USMPlacementMemory(const Internal::Memory::USMAllocator& memory) noexcept
            : memory_(memory)
        {
        }

        std::optional<std::size_t> USMPlacementMemory::recorded_device(const void* address) const
        {
            return memory_.device_of(address);
        }

        std::optional<std::size_t> USMPlacementMemory::owning_device(const void* address) const
        {
            return memory_.owning_device(address);
        }

        std::size_t USMPlacementMemory::device_count() const
        {
            return memory_.device_count();
        }

        bool USMPlacementMemory::shares_context(std::size_t first, std::size_t second) const
        {
            return memory_.shares_context(first, second);
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
