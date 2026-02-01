#pragma once

#include <mutex>
#include <atomic>
#include <cassert>

#include "../common/logging/logger.h"
#include "memory_block.h"

#ifdef _MSC_VER

    #include <intrin.h>

#endif

#define MAX_BINS 64
#define MAX_POOL_INSTANCES 16

namespace SushiBLAS 
{
    class MemoryPool : public RefCounted 
    {
        private:
            struct LocalPool 
            {
                MemoryBlock* bin_free_lists[MAX_BINS]{};
                MemoryBlock* pending_head = nullptr;
                MemoryBlock* pending_tail = nullptr;

                // Reentrancy Guard: Callback sırasında liste manipülasyonunu önler
                bool is_processing = false;
                MemoryBlock* deferred_free_head = nullptr;

                ~LocalPool() 
                {
                    // TODO: In the actual implementation, all lists should be moved to global_bins here.
                }
            };

            // Her pool instance'ı için ayrı TLS alanı
            static thread_local LocalPool local_pools[MAX_POOL_INSTANCES];
            static std::atomic<int> pool_instance_counter;

            int pool_id;
            std::mutex global_mutex;
            MemoryBlock* global_bins[MAX_BINS]{};

            // O(1) Bin Indexing: 1024 tabanlı log2 hesaplama
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

        public:
            MemoryPool() : pool_id(pool_instance_counter.fetch_add(1) % MAX_POOL_INSTANCES) {}

            MemoryBlock* allocate(size_t size) 
            {
                int bin_idx = get_bin_idx(size);
                LocalPool& lp = local_pools[pool_id];

                // 1. Kendi yerel pending listesini tara (O(N) linked list)
                process_local_pending(lp);

                // 2. Local bin kontrolü (O(1))
                if (lp.bin_free_lists[bin_idx]) 
                {
                    SB_LOG_DEBUG("MemoryPool: Local Cache Hit | Bin: {} | Size: {}", bin_idx, size);

                    MemoryBlock* b = lp.bin_free_lists[bin_idx];
                    lp.bin_free_lists[bin_idx] = b->next;
                    b->status = BlockStatus::ALLOCATED;
                    b->next = nullptr;
                    return b;
                }

                // 3. Global bin'den çal (Mutex Lock)
                {
                    std::lock_guard<std::mutex> lock(global_mutex);

                    if (global_bins[bin_idx]) 
                    {
                        MemoryBlock* b = global_bins[bin_idx];
                        global_bins[bin_idx] = b->next;
                        b->status = BlockStatus::ALLOCATED;
                        b->next = nullptr;
                        return b;
                    }
                }

                return nullptr; // Havuzda yok, Device yeni yer açmalı
            }

            void deallocate(MemoryBlock* block, sushi_ptr<Event> ready_event, const char* tag) 
            {
                SB_THROW_IF(block->status == BlockStatus::FREE, "MemoryPool: Double free detected for block at {}", block->ptr);

                LocalPool& lp = local_pools[pool_id];
                block->tag = tag;

                // Reentrancy Guard: Eğer şu an tarama yapıyorsak, bloğu erteleme listesine at
                if (lp.is_processing) 
                {
                    block->next = lp.deferred_free_head;
                    lp.deferred_free_head = block;
                    return;
                }

                if (ready_event) 
                {
                    SB_LOG_DEBUG("MemoryPool: Block PENDING (Event: {}) | Tag: {}", ready_event->get_name(), tag);

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
                        
                        // Listeden çıkar
                        if (prev) prev->next = next_node;
                        else lp.pending_head = next_node;
                        if (curr == lp.pending_tail) lp.pending_tail = prev;

                        // Yerel bin'e geri ver
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

                // Ertelenmiş (nested) deallocate çağrılarını işle
                while (lp.deferred_free_head) 
                {
                    MemoryBlock* b = lp.deferred_free_head;
                    lp.deferred_free_head = b->next;
                    // Bu noktada is_processing false olduğu için re-entry güvenli
                    deallocate(b, b->ready_event, b->tag);
                }
            }
    };

    std::atomic<int> MemoryPool::pool_instance_counter{0};
    thread_local MemoryPool::LocalPool MemoryPool::local_pools[MAX_POOL_INSTANCES];

} // namespace SushiBLAS