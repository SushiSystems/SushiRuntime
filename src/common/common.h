#pragma once

#include <cstddef>

namespace SushiBLAS 
{
    // Global Constants
    inline constexpr size_t CACHE_LINE_SIZE = 64;

    inline constexpr size_t MAX_BINS = 64;
    inline constexpr size_t MAX_POOL_INSTANCES = 16;

    inline constexpr size_t MAX_SUPPORTED_DEVICES = 16;

    // Could add other global configuration here, like alignment requirements, 
    // default pool sizes, etc.
}
