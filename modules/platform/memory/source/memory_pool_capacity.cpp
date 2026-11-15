/**************************************************************************/
/* memory_pool_capacity.cpp                                               */
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
 * @file memory_pool_capacity.cpp
 * @brief Implements usable_pool_bytes.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/memory_pool_capacity.hpp>

#include <algorithm>
#include <cstdint>
#include <map>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        std::size_t usable_pool_bytes(const std::vector<const MemorySpace*>& spaces)
        {
            std::map<std::uint32_t, const MemorySpace*> speaker_of_pool;
            for (const MemorySpace* space : spaces)
            {
                if (!space)
                    continue;
                const MemorySpace*& speaker = speaker_of_pool[space->shared_pool()];
                if (!speaker || space->id() < speaker->id())
                    speaker = space;
            }

            std::size_t total = 0;
            for (const auto& pool : speaker_of_pool)
            {
                const MemorySpace& space = *pool.second;
                total += space.capacity_bytes() - std::min(space.reserve_bytes(), space.capacity_bytes());
            }
            return total;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
