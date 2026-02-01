#include <iostream>

#include "../common/logging/logger.h"
#include "../device/device.h"
#include "memory_block.h"

namespace SushiBLAS 
{
    MemoryBlock::~MemoryBlock() 
    {
        SB_LOG_DEBUG("MemoryBlock: Destroying block at {} | Size: {} | Device: {}", ptr, size, device_id);
        
        if (owner_device) 
        {
            owner_device->destroy_block(this);
        }
    }
} // namespace SushiBLAS
