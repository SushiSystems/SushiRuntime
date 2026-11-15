/**************************************************************************/
/* usm_instance_allocator.hpp                                             */
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
 * @file usm_instance_allocator.hpp
 * @brief Declares USMInstanceAllocator, the InstanceAllocator that makes instances as USM.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <optional>

#include <SushiRuntime/coherence/instance_allocator.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Makes a device space's instance as device USM on that device, and the host
         *        space's as host USM in the context of device 0.
         *
         * The port co-owns the allocator, so a planned node that holds the port keeps it alive.
         */
        class USMInstanceAllocator final : public InstanceAllocator
        {
            public:
                /** @brief Builds the port over allocator. */
                explicit USMInstanceAllocator(std::shared_ptr<Memory::USMAllocator> allocator) noexcept;

                [[nodiscard]] void* allocate(MemorySpaceID space, std::size_t bytes,
                                             std::size_t alignment) override;

                void release(MemorySpaceID space, void* address) noexcept override;

                [[nodiscard]] std::optional<Memory::USMKind> kind_of(MemorySpaceID space) const noexcept override;

            private:
                /// @brief Allocator the instances come from.
                std::shared_ptr<Memory::USMAllocator> allocator_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
