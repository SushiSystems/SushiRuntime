/**************************************************************************/
/* usm_placement_memory.hpp                                               */
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
 * @file usm_placement_memory.hpp
 * @brief Declares USMPlacementMemory, the PlacementMemory that answers from a HandleResolver and a USMAllocator.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/task/placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        class USMAllocator;
    }

    namespace Internal::Graph
    {
        /**
         * @brief Answers a handle's device from its instance in a HandleResolver, and context sharing from the USMAllocator.
         * @see modules/engine/task/README.md
         */
        class SUSHIRUNTIME_API USMPlacementMemory final : public PlacementMemory
        {
            public:
                /**
                 * @brief Views @p memory and @p handles.
                 * @param memory,handles Must outlive this object.
                 */
                USMPlacementMemory(const Internal::Memory::USMAllocator& memory,
                                   const HandleResolver& handles) noexcept;

                [[nodiscard]] std::optional<std::size_t> recorded_device(GlobalHandle handle) const override;
                [[nodiscard]] std::optional<std::size_t> owning_device(GlobalHandle handle) const override;
                [[nodiscard]] std::size_t device_count() const override;
                [[nodiscard]] bool shares_context(std::size_t first, std::size_t second) const override;

            private:
                const Internal::Memory::USMAllocator& memory_;
                const HandleResolver& handles_;
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
