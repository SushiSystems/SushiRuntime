/**************************************************************************/
/* file_memory_space.cpp                                                  */
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
 * @file file_memory_space.cpp
 * @brief Implements FileMemorySpace.
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/file_memory_space.hpp>

#include <utility>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Value of the instance token: the first byte of the second page, which no allocation returns. */
            constexpr std::uintptr_t K_INSTANCE_TOKEN = 4096;
        } // namespace

        FileMemorySpace::FileMemorySpace(MemorySpaceID id, FileRange range)
            : id_(id), range_(std::move(range))
        {

        }

        void* FileMemorySpace::instance_address() noexcept
        {
            return reinterpret_cast<void*>(K_INSTANCE_TOKEN);
        }

        std::uint64_t FileMemorySpace::file_offset(const void* address) const noexcept
        {
            return range_.offset + (reinterpret_cast<std::uintptr_t>(address) - K_INSTANCE_TOKEN);
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
