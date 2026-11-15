/**************************************************************************/
/* instance_allocator.hpp                                                 */
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
 * @file instance_allocator.hpp
 * @brief Declares InstanceAllocator, the port that makes and frees the instance of a buffer on a space.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>

#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Makes and frees the memory of one instance on one memory space. */
        class InstanceAllocator
        {
            public:
                virtual ~InstanceAllocator() = default;

                /**
                 * @brief Returns bytes of memory on space, aligned to alignment.
                 * @return Null when the space refused the memory.
                 */
                [[nodiscard]] virtual void* allocate(MemorySpaceID space, std::size_t bytes,
                                                     std::size_t alignment) = 0;

                /** @brief Frees what allocate() returned for space; null is ignored. */
                virtual void release(MemorySpaceID space, void* address) noexcept = 0;

                /**
                 * @brief Returns the kind of USM allocate() makes on space, which the residency
                 *        directory records with the instance.
                 * @return Nothing when the memory is not USM.
                 */
                [[nodiscard]] virtual std::optional<Memory::USMKind> kind_of(MemorySpaceID space) const noexcept = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
