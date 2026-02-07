#include <iostream>

#include "../common/logging/logger.h"
#include "../device/device.h"
#include "memory_block.h"

namespace SushiBLAS 
{
    MemoryBlock::~MemoryBlock() 
    {
        // TODO: Ilerde detaylı memory leak analizi için buraya block ID loglaması eklenebilir.
        SB_LOG_DEBUG("MemoryBlock: Logical destructor called (Address: {}) | Size: {} | Device: {}", 
                     ptr, size, device_id);
        
        // AŞAMA 0.1 FIX:
        // owner_device->destroy_block(this) KESİNLİKLE ÇAĞRILMAMALI.
        // Bu blok bir havuza (pool) aittir. Pool yok olduğunda veya resetlendiğinde
        // toplu temizlik yapılır. Kendi kendini yok etmesi double-free'ye yol açar.
    }
} // namespace SushiBLAS