/**************************************************************************/
/* system_memory_probe.cpp                                                */
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
 * @file system_memory_probe.cpp
 * @brief Implements SystemMemoryProbe on Windows and on POSIX systems.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/system_memory_probe.hpp>

#include <cstddef>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        ProbedMemory SystemMemoryProbe::probe() const
        {
            ProbedMemory measured;
#if defined(_WIN32)
            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(status);
            if (GlobalMemoryStatusEx(&status) != 0)
            {
                measured.total_bytes = static_cast<std::size_t>(status.ullTotalPhys);
                measured.free_bytes = static_cast<std::size_t>(status.ullAvailPhys);
            }
#else
            const long page_bytes = sysconf(_SC_PAGESIZE);
            const long total_pages = sysconf(_SC_PHYS_PAGES);
            const long free_pages = sysconf(_SC_AVPHYS_PAGES);
            if (page_bytes > 0 && total_pages > 0 && free_pages >= 0)
            {
                measured.total_bytes = static_cast<std::size_t>(page_bytes) * static_cast<std::size_t>(total_pages);
                measured.free_bytes = static_cast<std::size_t>(page_bytes) * static_cast<std::size_t>(free_pages);
            }
#endif
            return measured;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
