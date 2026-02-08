#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace SushiBLAS 
{
    struct HardwareInfo 
    {
        std::string device_name;
        bool is_cuda = false;
        
        int major_version = 0;
        int minor_version = 0;
    
        int core_count = 0;
        int max_threads_per_block = 0;
        
        size_t total_global_memory = 0;
        size_t shared_memory_per_block = 0;
        
        int clock_rate_khz = 0;
        
        // TODO: Add cache sizes
        int l2_cache_size = 0;

        std::string to_string() const 
        {
            return std::format("{} (Arch: {}.{}, Cores: {}, Mem: {} MB)", 
                device_name, major_version, minor_version, core_count, total_global_memory / (1024 * 1024));
        }
    };
} // namespace SushiBLAS
