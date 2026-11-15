/**************************************************************************/
/* pinned_host_memory.hpp                                                 */
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
 * @file pinned_host_memory.hpp
 * @brief Declares PinnedHostMemory, the port that makes and frees pinned host memory.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Makes and frees host memory that the devices of the runtime copy to and from.
         *
         * The staging ring takes its slots from it, so the ring never names a device or an
         * allocator.
         */
        class PinnedHostMemory
        {
            public:
                virtual ~PinnedHostMemory() = default;

                /** @brief Returns bytes of pinned host memory, or null when there is none. */
                [[nodiscard]] virtual void* allocate(std::size_t bytes) = 0;

                /**
                 * @brief Frees an address allocate() returned.
                 * @pre address came from allocate() and is not freed.
                 */
                virtual void release(void* address) noexcept = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
