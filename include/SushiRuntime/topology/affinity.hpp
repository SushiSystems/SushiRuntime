/****************************************************************/
/* affinity.hpp                                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
         *
         * Uses the OS affinity API (SetThreadAffinityMask on Windows,
         * pthread_setaffinity_np on POSIX) to bind the thread so the
         * scheduler never migrates it to a different core.
         *
         * @param core_id Logical core index (0-based). Must be a valid index
         *                for the running system; out-of-range values are logged
         *                as warnings and silently ignored by the OS.
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