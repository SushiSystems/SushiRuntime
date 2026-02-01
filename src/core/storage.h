#pragma once

#include <cstddef>
#include <cassert>

#include "../device/device.h"
#include "sushi_ptr.h"

#define CACHE_LINE_SIZE 64

namespace SushiBLAS 
{
    class Storage : public RefCounted 
    {
        public:
            void* data_ptr = nullptr;

            size_t size_bytes = 0;       
            size_t requested_bytes = 0;

            sushi_ptr<Device> device;

            Storage(sushi_ptr<Device> dev, size_t n_bytes) : device(dev), requested_bytes(n_bytes) 
            {
                SB_THROW_IF(!device, "Storage: Device pointer cannot be null");
                SB_THROW_IF(n_bytes == 0, "Storage: Requested size cannot be zero");

                size_bytes = n_bytes;
                if (size_bytes % CACHE_LINE_SIZE != 0) 
                {
                    size_bytes += CACHE_LINE_SIZE - (size_bytes % CACHE_LINE_SIZE);
                }

                data_ptr = device->allocate(size_bytes);

                SB_THROW_IF(!data_ptr, "Storage: Failed to allocate {} bytes on device {}", 
                            size_bytes, device->get_name());

                SB_LOG_DEBUG("Storage Created: {} bytes (requested: {}) on {}", 
                            size_bytes, requested_bytes, device->get_name());
            }

            Storage(const Storage&) = delete;
            Storage& operator=(const Storage&) = delete;

        protected:
            ~Storage() override 
            {
                SB_THROW_IF(get_ref_count() != 0, "Storage: RefCount must be 0 during destruction! Current: {}", get_ref_count());

                if (data_ptr && device) 
                {
                    device->deallocate(data_ptr, nullptr, "Storage Destructor");
                    SB_LOG_DEBUG("Storage: Deallocated {} bytes from {}", size_bytes, device->get_name());
                }
            }
    };
} // namespace SushiBLAS