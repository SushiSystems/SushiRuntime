#pragma once

#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cstddef>

#include "../common/logging/logger.h"
#include "../memory/memory_pool.h"
#include "../core/sushi_ptr.h"
#include "../common/common.h"
#include "../stream/stream.h"
#include "../event/event.h"
#include "hardware_info.h"

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

            // System Allocation Tracking
            mutable std::mutex allocation_mutex;
            std::vector<void*> system_allocations;

            // Block Registry: void* addr -> MemoryBlock*
            // Map-based tracking instead of in-place headers.
            std::unordered_map<void*, MemoryBlock*> block_registry;

        public:
            virtual ~Device() 
            {
                // Ensure MemoryPool is destroyed BEFORE we delete the blocks.
                // MemoryPool destructor triggers cache flushing, which accesses the blocks.
                // If we delete blocks first, MemoryPool will access freed memory -> Access Violation.
                if (pool) pool->invalidate();
                pool = nullptr;

                std::lock_guard<std::mutex> lock(allocation_mutex);
                
                for (auto& [ptr, block] : block_registry)
                {
                    delete block;
                }
                block_registry.clear();

                if (!system_allocations.empty())
                {                    
                    for (void* ptr : system_allocations)
                    {
                        free_raw(ptr);
                    }
                    system_allocations.clear();
                }
            }

            virtual void* allocate(size_t size) 
            {
                MemoryBlock* block = pool->allocate(size);

                if (!block) 
                {
                    constexpr size_t align = CACHE_LINE_SIZE;
                    void* raw_mem = allocate_tracked(size);
                    
                    SB_THROW_IF(raw_mem == nullptr, "Physical allocation failed for {} bytes on Device {}", 
                                size, get_name());

                    MemoryBlock* block_ptr = new MemoryBlock(
                        raw_mem, 
                        size, 
                        align, 
                        0,
                        this->device_id
                    );

                    block_ptr->owner_device = this;
                    block_ptr->status = BlockStatus::ALLOCATED;

                    {
                        std::lock_guard<std::mutex> lock(allocation_mutex);
                        block_registry[raw_mem] = block_ptr;
                    }

                    block = block_ptr;
                }

                return block->ptr;
            }

            virtual void deallocate(void* ptr, sushi_ptr<Event> e, const char* tag = "cleanup") 
            {
                if (!ptr) return;

                MemoryBlock* block = nullptr;
                {
                    std::lock_guard<std::mutex> lock(allocation_mutex);
                    auto it = block_registry.find(ptr);
                    if (it != block_registry.end())
                    {
                        block = it->second;
                    }
                }
                
                SB_THROW_IF(!block, "Attempted to deallocate unknown pointer {} on device {}", ptr, get_name());
                
                SB_ASSERT(block->device_id == this->get_id(), 
                            "Device mismatch. Block is from dev {} but dealloc on dev {}", 
                            block->device_id, this->get_id());

                pool->deallocate(block, e, tag);
            }

            virtual void destroy_block(MemoryBlock* block)
            {
                if (!block) return;
                
                void* raw_ptr = block->ptr;
                
                {
                    std::lock_guard<std::mutex> lock(allocation_mutex);
                    block_registry.erase(raw_ptr);
                }

                delete block;
                deallocate_tracked(raw_ptr);
            }

            virtual int get_id() const = 0;
            virtual const char* get_name() const = 0;
            virtual sushi_ptr<Stream> create_stream() = 0;
            virtual sushi_ptr<Stream> get_default_stream() = 0;
            virtual sushi_ptr<Event> create_event(const char* name) = 0;
            virtual void synchronize() = 0;

            virtual void copy_h2d(void* dst, const void* src, size_t size, Stream* stream) = 0;
            virtual void copy_d2h(void* dst, const void* src, size_t size, Stream* stream) = 0;
            virtual void copy_d2d(void* dst, Device* src_dev, const void* src, size_t size, Stream* stream) = 0;

            virtual HardwareInfo get_hardware_info() const = 0;

        protected:
            Device(int32_t id) : device_id(id), pool(make_sushi<MemoryPool>(id)) { }
            
            virtual void* allocate_raw(size_t size) = 0;
            virtual void free_raw(void* ptr) { (void)ptr; };

        private:
            void* allocate_tracked(size_t size)
            {
                void* ptr = allocate_raw(size);
                if (ptr)
                {
                    std::lock_guard<std::mutex> lock(allocation_mutex);
                    system_allocations.push_back(ptr);
                }
                return ptr;
            }

            void deallocate_tracked(void* ptr)
            {
                if (!ptr) return;
                
                {
                    std::lock_guard<std::mutex> lock(allocation_mutex);
                    auto it = std::find(system_allocations.begin(), system_allocations.end(), ptr);
                    
                    if (it != system_allocations.end())
                    {
                        *it = system_allocations.back();
                        system_allocations.pop_back();
                        free_raw(ptr);
                    }
                    else
                    {
                        SB_LOG_ERROR("Double free or unknown pointer: {}", ptr);
                    }
                }
            }
    };
} // namespace SushiBLAS