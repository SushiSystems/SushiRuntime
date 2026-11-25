/**************************************************************************/
/* page_protection.hpp                                                    */
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
 * @file page_protection.hpp
 * @brief Declares the page calls GuardedDevicePages maps, protects and releases its memory with.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief What the threads of the process may do with a range of pages. */
        enum class PageAccess : std::uint8_t
        {
            None,     /**< Any read or write faults. */
            ReadWrite /**< Reads and writes succeed. */
        };

        /** @brief Returns the bytes of one page, the unit page_protection maps and protects. */
        [[nodiscard]] SUSHIRUNTIME_API std::size_t page_bytes() noexcept;

        /**
         * @brief Maps zeroed pages of their own, with an access.
         * @return The first byte, on a page boundary, or null when the system refuses.
         * @pre bytes is a positive multiple of page_bytes().
         */
        [[nodiscard]] SUSHIRUNTIME_API void* map_pages(std::size_t bytes, PageAccess access) noexcept;

        /**
         * @brief Sets the access of pages map_pages() returned.
         * @return False when the system refuses.
         * @pre first and bytes name a whole range map_pages() returned and that is still mapped.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool protect_pages(void* first, std::size_t bytes, PageAccess access) noexcept;

        /**
         * @brief Unmaps pages map_pages() returned.
         * @pre first and bytes name a whole range map_pages() returned and that is still mapped.
         */
        SUSHIRUNTIME_API void unmap_pages(void* first, std::size_t bytes) noexcept;
    } // namespace Internal::Memory
} // namespace SushiRuntime
