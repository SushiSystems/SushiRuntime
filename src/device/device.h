#pragma once

#include "../common/logging/logger.h"
#include "../core/sushi_ptr.h"
#include "../core/storage.h"
#include "memory_pool.h"
#include "stream.h"
#include "event.h"

namespace SushiBLAS 
{
    enum class DeviceType 
    {
        CPU,
        CUDA
    };

    class Device : public RefCounted 
    {
        protected:
            sushi_ptr<MemoryPool> pool;
            int32_t device_id;

        public:
            virtual ~Device() = default;

            virtual void* allocate(size_t size) 
            {
                MemoryBlock* block = pool->allocate(size);

                if (!block) 
                {
                    constexpr size_t align = CACHE_LINE_SIZE;

                    size_t header_size = sizeof(MemoryBlock*) + sizeof(MemoryBlock);
                    size_t total_header = (header_size + align - 1) & ~(align - 1);

                    void* raw_mem = allocate_raw(total_header + size);
                    SB_THROW_IF(raw_mem == nullptr, "Memory: Physical allocation failed for {} bytes on Device {}", total_header + size, get_name());

                    uint8_t* base = static_cast<uint8_t*>(raw_mem);
                    MemoryBlock* block_ptr = new (base) MemoryBlock(
                        nullptr, 
                        size, 
                        align, 
                        total_header,
                        this->device_id
                    );

                    block_ptr->owner_device = this;

                    uint8_t* data_ptr = base + total_header;

                    *(reinterpret_cast<MemoryBlock**>(data_ptr) - 1) = block_ptr;

                    block_ptr->ptr = data_ptr;
                    block = block_ptr;
                }

                return block->ptr;
            }

            virtual void deallocate(void* ptr, sushi_ptr<Event> e, const char* tag = "cleanup") 
            {
                if (!ptr) return;

                MemoryBlock* block = MemoryBlock::from_ptr(ptr);
                
                SB_THROW_IF(block->device_id != this->get_id(), 
                            "Memory: Device mismatch. Trying to deallocate block from device {} on device {}", 
                            block->device_id, this->get_id());

                pool->deallocate(block, e, tag);
            }

            virtual sushi_ptr<Stream> create_stream() = 0;
            virtual void synchronize() = 0;

            virtual const char* get_name() const = 0;
            virtual int get_id() const = 0;
            virtual sushi_ptr<Stream> get_default_stream() = 0;
            virtual sushi_ptr<Event> create_event(const char* name) = 0;

            virtual void copy_h2d(void* dst, const void* src, size_t size, Stream* stream) = 0;
            virtual void copy_d2h(void* dst, const void* src, size_t size, Stream* stream) = 0;
            virtual void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* stream) = 0;

            virtual void destroy_block(MemoryBlock* block)
            {
                free_raw(block);
            }

        protected:
            Device(int32_t id) : device_id(id), pool(make_sushi<MemoryPool>()) {}
            virtual void* allocate_raw(size_t size) = 0;
            virtual void free_raw(void* ptr) = 0;
        };
        
} // namespace SushiBLAS