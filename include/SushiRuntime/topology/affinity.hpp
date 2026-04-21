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
         * @brief İşletim sistemini atlayarak mevcut thread'i spesifik bir CPU çekirdeğine donanımsal olarak kilitler.
         * @param core_id Kilitlenecek mantıksal çekirdek ID'si (0, 1, 2...)
         */
        inline void pin_current_thread_to_core(int core_id) 
        {
            #ifdef _WIN32
                HANDLE thread = GetCurrentThread();
                DWORD_PTR mask = (1ULL << core_id); 
                
                if (SetThreadAffinityMask(thread, mask) == 0) 
                    SR_LOG_WARN("Failed to set thread affinity for core {} on Windows. Error: {}", core_id, GetLastError());
                else
                    SR_LOG_DEBUG("Thread successfully pinned to core {}", core_id);
            #else
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(core_id, &cpuset);

                pthread_t current_thread = pthread_self();    
                if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) 
                    SR_LOG_WARN("Failed to set thread affinity for core {} on POSIX system.", core_id);
                else
                    SR_LOG_DEBUG("Thread successfully pinned to core {}", core_id);
            #endif
        }
    }
}