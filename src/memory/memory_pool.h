#pragma once

#include <mutex>
#include <atomic>
#include <cassert>
#include <algorithm>
#include <unordered_map>

#include "../common/logging/logger.h"
#include "../common/common.h"
#include "memory_block.h"

#ifdef _MSC_VER
    #include <intrin.h>
#endif

namespace SushiBLAS 
{
    class MemoryPool : public RefCounted 
    {
        private:
            struct alignas(CACHE_LINE_SIZE) LocalPool 
            {
                MemoryBlock* bin_free_lists[MAX_BINS]{};
                MemoryBlock* pending_head = nullptr;
                MemoryBlock* pending_tail = nullptr;

                bool is_processing = false;
                MemoryBlock* deferred_free_head = nullptr;
                
                sushi_ptr<MemoryPool> owner_pool;

                LocalPool(MemoryPool* p) : owner_pool(p) {}
                LocalPool() = default;

                ~LocalPool() 
                {
                    // If we are in global destruction, accessing sushi_ptr or the pool 
                    // might be unsafe if the control block is already gone.
                    // During process exit, we don't really need to flush to global anyway.
                    if (owner_pool) 
                    {
                        try 
                        {
                            owner_pool->flush_cache_to_global(this);
                        } 
                        catch (...) 
                        {
                            // Suppress any errors during shutdown
                        }
                    }
                }
                
                LocalPool(const LocalPool&) = delete;
                LocalPool& operator=(const LocalPool&) = delete;
                LocalPool(LocalPool&&) = default;
            };

            inline static thread_local std::unordered_map<MemoryPool*, LocalPool> thread_caches;

            int32_t device_id_;
            std::mutex global_mutex;
            MemoryBlock* global_bins[MAX_BINS]{};
            
            std::atomic<bool> invalidated{false}; // Prevent use-after-free during destruction

            inline int get_bin_idx(size_t size) const 
            {
                if (size <= 1024) return 0;

                #ifdef _MSC_VER

                    unsigned long index;
                    _BitScanReverse64(&index, size - 1);
                    int idx = static_cast<int>(index - 9); 
                    
                #else
                    int idx = 63 - __builtin_clzll(size - 1) - 9;
                #endif

                return (idx < 0) ? 0 : (idx >= MAX_BINS ? MAX_BINS - 1 : idx);
            }

            void flush_cache_to_global(LocalPool* lp)
            {
                // If the pool is invalidated (e.g. Device is destroying blocks), 
                // DO NOT touch the blocks, as they might be dangling pointers.
                if (invalidated) return;

                std::lock_guard<std::mutex> lock(global_mutex);

                for (int i = 0; i < MAX_BINS; ++i)
                {
                    MemoryBlock* curr = lp->bin_free_lists[i];
                    while (curr)
                    {
                        MemoryBlock* next = curr->next;
                        
                        SB_ASSERT(curr->status == BlockStatus::FREE, "Flushing non-free block!");
                        SB_ASSERT(curr->device_id == this->device_id_, "Device ID Mismatch during flush!");
                        
                        curr->next = global_bins[i];
                        global_bins[i] = curr;
                        
                        curr = next;
                    }
                    lp->bin_free_lists[i] = nullptr;
                }

                // TODO: Move pending blocks to a global orphan list to prevent leaks on thread exit.
                if (lp->pending_head)
                {
                    SB_LOG_WARN("Thread exiting with pending blocks. Potential leak if events never trigger.");
                }

                SB_ASSERT(lp->pending_head == nullptr, "Pending blocks during thread exit");
                SB_ASSERT(lp->deferred_free_head == nullptr, "Deferred blocks during thread exit");
            }

            LocalPool& get_local_pool() 
            {
                auto it = thread_caches.find(this);
                if (it == thread_caches.end()) 
                {
                    auto result = thread_caches.emplace(
                        std::piecewise_construct,
                        std::forward_as_tuple(this),
                        std::forward_as_tuple(this)
                    );
                    return result.first->second;
                }
                return it->second;
            }

        public:
            MemoryPool(int32_t dev_id) : device_id_(dev_id) {}
            
            ~MemoryPool() override 
            {
                thread_caches.erase(this);

                std::lock_guard<std::mutex> lock(global_mutex);
                for(int i = 0; i < MAX_BINS; ++i)
                {
                    global_bins[i] = nullptr;
                }
            }
            
            void invalidate() 
            {
                invalidated = true;
            }

            MemoryBlock* allocate(size_t size) 
            {
                int bin_idx = get_bin_idx(size);
                LocalPool& lp = get_local_pool();

                process_local_pending(lp);

                if (lp.bin_free_lists[bin_idx]) 
                {
                    MemoryBlock* b = lp.bin_free_lists[bin_idx];

                    SB_ASSERT(b->size >= size, "Bin logic error");

                    lp.bin_free_lists[bin_idx] = b->next;
                    b->status = BlockStatus::ALLOCATED;
                    b->next = nullptr;
                    return b;
                }

                {
                    std::lock_guard<std::mutex> lock(global_mutex);

                    if (global_bins[bin_idx]) 
                    {
                        MemoryBlock* b = global_bins[bin_idx];
                        
                        SB_ASSERT(b->device_id == this->device_id_, "Global bin contamination!");

                        global_bins[bin_idx] = b->next;
                        b->status = BlockStatus::ALLOCATED;
                        b->next = nullptr;
                        return b;
                    }
                }

                return nullptr; // Allocate new from OS (Device handles this)
            }

            void deallocate(MemoryBlock* block, sushi_ptr<Event> ready_event, const char* tag) 
            {
                SB_THROW_IF(block->status == BlockStatus::FREE, "Double free detected for block at {}", block->ptr);
                
                SB_ASSERT(block->device_id == this->device_id_, "Deallocating block from wrong device");

                LocalPool& lp = get_local_pool();
                block->tag = tag;

                // Reetrancy Guard
                if (lp.is_processing) 
                {
                    block->next = lp.deferred_free_head;
                    lp.deferred_free_head = block;
                    return;
                }

                if (ready_event) 
                {
                    block->status = BlockStatus::PENDING;
                    block->ready_event = ready_event;
                    block->next = nullptr;
                    
                    if (!lp.pending_tail) 
                    {
                        lp.pending_head = lp.pending_tail = block;  
                    } 
                    else 
                    {
                        lp.pending_tail->next = block;
                        lp.pending_tail = block;
                    }
                } 
                else 
                {
                    return_to_local_bin(lp, block);
                }
            }

        private:
            void return_to_local_bin(LocalPool& lp, MemoryBlock* b) 
            {
                int b_idx = get_bin_idx(b->size);
                b->status = BlockStatus::FREE;
                b->ready_event = nullptr;
                b->next = lp.bin_free_lists[b_idx];
                lp.bin_free_lists[b_idx] = b;
            }

            void process_local_pending(LocalPool& lp) 
            {
                lp.is_processing = true;

                MemoryBlock* prev = nullptr;
                MemoryBlock* curr = lp.pending_head;

                while (curr) 
                {
                    if (curr->is_ready()) 
                    {
                        MemoryBlock* next_node = curr->next;
                        
                        if (prev) 
                            prev->next = next_node;
                        else 
                            lp.pending_head = next_node;

                        if (curr == lp.pending_tail) 
                            lp.pending_tail = prev;

                        return_to_local_bin(lp, curr);
                        curr = next_node;
                    }
                    else 
                    {
                        prev = curr;
                        curr = curr->next;
                    }
                }

                lp.is_processing = false;

                while (lp.deferred_free_head) 
                {
                    MemoryBlock* b = lp.deferred_free_head;
                    lp.deferred_free_head = b->next;
                    deallocate(b, b->ready_event, b->tag);
                }
            }
    };
    
} // namespace SushiBLAS