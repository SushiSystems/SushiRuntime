/**************************************************************************/
/* affinity.hpp                                                           */
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
 * @file affinity.hpp
 * @brief Defines pin_current_thread_to_core.
 * @author Mustafa Garip
 * @see include/SushiRuntime/topology/README.md
 */

#pragma once

#include <thread>
#include <system_error>
#include <SushiRuntime/core/logger.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
#endif

namespace SushiRuntime 
{
    namespace Topology 
    {
        /**
         * @brief Pins the calling thread to a specific logical CPU core.
         * @param core_id Logical core index, 0-based; an out-of-range value is logged and ignored.
         * @see include/SushiRuntime/topology/README.md
         */
        inline void pin_current_thread_to_core(int core_id)
        {
            #ifdef _WIN32
                HANDLE thread = GetCurrentThread();
                DWORD_PTR mask = (1ULL << core_id); 
                
                if (SetThreadAffinityMask(thread, mask) == 0)
                {
                    SR_LOG_WARN("Failed to set thread affinity for core {} on Windows. Error: {}", core_id, GetLastError());
                }
                else
                {
                    SR_LOG_DEBUG("Thread successfully pinned to core {}", core_id);
                }
            #else
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(core_id, &cpuset);

                pthread_t current_thread = pthread_self();    
                if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) 
                {
                    SR_LOG_WARN("Failed to set thread affinity for core {} on POSIX system.", core_id);
                } 
                else 
                {
                    SR_LOG_DEBUG("Thread successfully pinned to core {}", core_id);
                }
            #endif
        }
    }
}