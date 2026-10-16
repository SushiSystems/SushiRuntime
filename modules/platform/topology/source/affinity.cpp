/**************************************************************************/
/* affinity.cpp                                                           */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file affinity.cpp
 * @brief Pins the calling thread to one logical core through the OS affinity call.
 * @author Mustafa Garip
 */

#include <SushiRuntime/topology/affinity.hpp>

#include <cstdint>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
    #include <unistd.h>
#endif

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/topology/topology_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
#ifdef _WIN32
            /** @brief Returns how many logical cores the OS can name, over every processor group. */
            int logical_core_limit() noexcept
            {
                return static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            }

            /**
             * @brief Binds the calling thread to one core and returns the OS error, 0 when bound.
             * @pre core_id is in [0, logical_core_limit()).
             */
            std::uint32_t bind_calling_thread(int core_id) noexcept
            {
                DWORD index_in_group = static_cast<DWORD>(core_id);
                const WORD group_count = GetActiveProcessorGroupCount();
                for (WORD group = 0; group < group_count; ++group)
                {
                    // A group holds at most 64 cores, so the shift below stays under 64.
                    const DWORD cores_in_group = GetActiveProcessorCount(group);
                    if (index_in_group < cores_in_group)
                    {
                        GROUP_AFFINITY affinity = {};
                        affinity.Group = group;
                        affinity.Mask = static_cast<KAFFINITY>(1) << index_in_group;
                        if (SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0)
                            return 0;
                        return static_cast<std::uint32_t>(GetLastError());
                    }
                    index_in_group -= cores_in_group;
                }
                return static_cast<std::uint32_t>(ERROR_INVALID_PARAMETER);
            }
#else
            /** @brief Returns how many logical cores the OS can name in one cpu_set_t. */
            int logical_core_limit() noexcept
            {
                const long configured = sysconf(_SC_NPROCESSORS_CONF);
                if (configured < 1 || configured > static_cast<long>(CPU_SETSIZE))
                    return static_cast<int>(CPU_SETSIZE);
                return static_cast<int>(configured);
            }

            /**
             * @brief Binds the calling thread to one core and returns the OS error, 0 when bound.
             * @pre core_id is in [0, logical_core_limit()).
             */
            std::uint32_t bind_calling_thread(int core_id) noexcept
            {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(core_id, &cpuset);
                return static_cast<std::uint32_t>(
                    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset));
            }
#endif
        } // namespace

        void pin_current_thread_to_core(int core_id)
        {
            const int core_limit = logical_core_limit();
            if (core_id < 0 || core_id >= core_limit)
            {
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "Core id {} is outside [0, {}); the thread is not pinned and keeps "
                            "its affinity.",
                            core_id, core_limit);
                return;
            }

            const std::uint32_t os_error = bind_calling_thread(core_id);
            if (os_error != 0)
            {
                SR_LOG_WARN(K_TOPOLOGY_LOG_CATEGORY, "The OS refused to pin the thread to core {} (error {}); the thread "
                            "keeps its affinity.",
                            core_id, os_error);
                return;
            }
            SR_LOG_DEBUG(K_TOPOLOGY_LOG_CATEGORY, "Thread successfully pinned to core {}", core_id);
        }
    } // namespace Topology
} // namespace SushiRuntime
