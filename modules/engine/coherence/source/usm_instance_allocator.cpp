/**************************************************************************/
/* usm_instance_allocator.cpp                                             */
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
 * @file usm_instance_allocator.cpp
 * @brief Implements USMInstanceAllocator.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/usm_instance_allocator.hpp>

#include <cstddef>
#include <optional>
#include <utility>

#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        USMInstanceAllocator::USMInstanceAllocator(std::shared_ptr<Memory::USMAllocator> allocator) noexcept
            : allocator_(std::move(allocator))
        {

        }

        void* USMInstanceAllocator::allocate(MemorySpaceID space, std::size_t bytes, std::size_t alignment)
        {
            const std::optional<std::size_t> device = Graph::device_of_space(space);
            if (device && *device >= allocator_->device_count())
                return nullptr;

            Memory::USMRequest request;
            request.kind = device ? Memory::USMKind::Device : Memory::USMKind::Host;
            request.bytes = bytes;
            request.alignment = alignment;
            request.device_index = device ? *device : 0;
            return allocator_->allocate(request);
        }

        void USMInstanceAllocator::release(MemorySpaceID, void* address) noexcept
        {
            allocator_->release(address);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
