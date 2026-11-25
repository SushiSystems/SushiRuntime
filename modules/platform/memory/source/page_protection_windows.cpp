/**************************************************************************/
/* page_protection_windows.cpp                                            */
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
 * @file page_protection_windows.cpp
 * @brief Implements the page calls of page_protection.hpp with VirtualAlloc and VirtualProtect.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/page_protection.hpp>

#if defined(_WIN32)

#include <cstddef>

#if !defined(NOMINMAX)
    #define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Returns the page protection constant of an access. */
            [[nodiscard]] DWORD protection_of(PageAccess access) noexcept
            {
                return access == PageAccess::ReadWrite ? PAGE_READWRITE : PAGE_NOACCESS;
            }
        } // namespace

        std::size_t page_bytes() noexcept
        {
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            return static_cast<std::size_t>(info.dwPageSize);
        }

        void* map_pages(std::size_t bytes, PageAccess access) noexcept
        {
            return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, protection_of(access));
        }

        bool protect_pages(void* first, std::size_t bytes, PageAccess access) noexcept
        {
            DWORD previous = 0;
            return VirtualProtect(first, bytes, protection_of(access), &previous) != 0;
        }

        void unmap_pages(void* first, std::size_t) noexcept
        {
            // MEM_RELEASE frees the whole reservation VirtualAlloc made and takes a size of 0.
            static_cast<void>(VirtualFree(first, 0, MEM_RELEASE));
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime

#endif // defined(_WIN32)
