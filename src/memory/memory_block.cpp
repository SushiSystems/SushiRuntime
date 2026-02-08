#include <iostream>

#include "../common/logging/logger.h"
#include "../device/device.h"
#include "memory_block.h"

namespace SushiBLAS 
{
    MemoryBlock::~MemoryBlock() 
    {
        // TODO: Add block ID logging for future memory leak analysis.
        SB_LOG_DEBUG("Logical destructor called (Address: {}) | Size: {} | Device: {}", 
                     ptr, size, device_id);
    }
} // namespace SushiBLAS