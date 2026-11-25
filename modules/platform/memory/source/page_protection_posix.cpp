/**************************************************************************/
/* page_protection_posix.cpp                                              */
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
 * @file page_protection_posix.cpp
 * @brief Implements the page calls of page_protection.hpp with mmap and mprotect.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/page_protection.hpp>

#if !defined(_WIN32)

#include <cstddef>

#include <sys/mman.h>
#include <unistd.h>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Returns the protection flags of an access. */
            [[nodiscard]] int protection_of(PageAccess access) noexcept
            {
                return access == PageAccess::ReadWrite ? (PROT_READ | PROT_WRITE) : PROT_NONE;
            }
        } // namespace

        std::size_t page_bytes() noexcept
        {
            const long bytes = sysconf(_SC_PAGESIZE);
            return bytes > 0 ? static_cast<std::size_t>(bytes) : std::size_t{4096};
        }

        void* map_pages(std::size_t bytes, PageAccess access) noexcept
        {
            void* const first = mmap(nullptr, bytes, protection_of(access), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            return first == MAP_FAILED ? nullptr : first;
        }

        bool protect_pages(void* first, std::size_t bytes, PageAccess access) noexcept
        {
            return mprotect(first, bytes, protection_of(access)) == 0;
        }

        void unmap_pages(void* first, std::size_t bytes) noexcept
        {
            static_cast<void>(munmap(first, bytes));
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime

#endif // !defined(_WIN32)
