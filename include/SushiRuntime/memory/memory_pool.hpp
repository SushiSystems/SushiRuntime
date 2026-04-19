/**************************************************************************/
/* memory_pool.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <bit>
#include <atomic>
#include <cstddef>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime 
{
    namespace Memory
    {
        /**
         * @brief Bitmask-based management for memory slots.
         * * Each uint64_t bitmask represents 64 slots in a page.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) MemoryPage 
        {
            /** @brief Start address of the page. */
            void* start_ptr;
            /** @brief Atomic mask where 0 means empty and 1 means full. */
            std::atomic<uint64_t> bitmask{0};
            /** @brief Next page in the linked list pool. */
            std::atomic<MemoryPage*> next{nullptr};
            /** @brief ID of the NUMA node where this page lives. */
            int numa_node_id;
            /** @brief Size of each slot in the page. */
            size_t slot_size;
            /** @brief The bucket index this page belongs to (used for TLC). */
            int bucket_idx;
        };

        /**
         * @brief Thread-local cache (TLC) for fast allocation.
         * * Allows each thread to access local slots without using locks.
         * Separated into buckets to prevent memory size mismatch bugs.
         */
        struct ThreadLocalCache 
        {
            /** @brief Number of slots stored in a single batch per bucket. */
            static constexpr size_t BATCH_SIZE = 256;
            /** @brief Number of bucket sizes (128, 256, 1024, 4096). */
            static constexpr size_t NUM_BUCKETS = 4;

            struct SlotInfo 
            {
                void* ptr;
                MemoryPage* owner;
                uint64_t alloc_id;
            };

            /** @brief Array of available memory slots per bucket. */
            SlotInfo slots[NUM_BUCKETS][BATCH_SIZE];
            /** @brief Number of slots currently in the cache per bucket. */
            size_t count[NUM_BUCKETS] = {0};
            
            /** @brief Pops a slot from the specific bucket cache, or returns nullptr. */
            inline void* pop(int bucket_idx, uint64_t current_alloc_id)
            {
                if (bucket_idx < 0 || bucket_idx >= NUM_BUCKETS || count[bucket_idx] == 0) 
                    return nullptr;

                SlotInfo& info = slots[bucket_idx][count[bucket_idx] - 1];
                
                if (info.alloc_id != current_alloc_id)
                {
                    count[bucket_idx] = 0;
                    return nullptr;
                }

                return slots[bucket_idx][--count[bucket_idx]].ptr;
            }
            
            /** @brief Pushes a free slot back to the bucket cache, returns false if full. */
            inline bool push(int bucket_idx, void* ptr, MemoryPage* owner, uint64_t current_alloc_id) 
            {
                if (bucket_idx < 0 || static_cast<size_t>(bucket_idx) >= NUM_BUCKETS || count[bucket_idx] >= BATCH_SIZE) 
                    return false;
                
                slots[bucket_idx][count[bucket_idx]++] = {ptr, owner, current_alloc_id};
                return true;
            }

            ~ThreadLocalCache() { }
        };

        /** @brief Global thread-local storage for the cache. */
        inline thread_local ThreadLocalCache t_cache;

        /**
         * @brief Memory pool that uses bitmasks to track free space.
         */
        class BitmaskMemoryPool 
        {
            public:
                /**
                 * @brief Allocates a single slot. Traverses the linked list of pages.
                 * @param root_page The starting memory page in the linked list pool.
                 * @return void* Pointer to the allocated memory, or nullptr if completely full.
                 */
                void* allocate_from_pool(MemoryPage* root_page) 
                {
                    MemoryPage* current_page = root_page;
                    
                    while (current_page != nullptr)
                    {
                        uint64_t current_mask = current_page->bitmask.load(std::memory_order_relaxed);
                        
                        while (current_mask != ~0ULL)
                        { 
                            // find the first zero bit (empty slot) using std::countr_one
                            int first_free = std::countr_one(current_mask);
                            if (first_free >= 64) break;

                            uint64_t new_mask = current_mask | (1ULL << first_free);
                            
                            // use atomic hardware compare-and-swap
                            if (current_page->bitmask.compare_exchange_weak(current_mask, new_mask, 
                                                                std::memory_order_acquire, 
                                                                std::memory_order_relaxed)) 
                            {
                                return static_cast<uint8_t*>(current_page->start_ptr) + (first_free * current_page->slot_size);
                            }
                        }
                        
                        // Page full or highly contested, move to next page in the chain
                        current_page = current_page->next.load(std::memory_order_acquire);
                    }
                    
                    return nullptr; // Entire pool chain is full
                }



                /**
                 * @brief Returns a memory slot back to its specific designated page.
                 * @param page The specific memory page that owns the slot.
                 * @param ptr The pointer to the memory being freed.
                 */
                void deallocate_to_page(MemoryPage& page, void* ptr) 
                {
                    size_t offset = static_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(page.start_ptr);
                    int slot_idx = static_cast<int>(offset / page.slot_size);
                    
                    // set the bit back to 0 atomically
                    page.bitmask.fetch_and(~(1ULL << slot_idx), std::memory_order_release);
                }
        };

    } // namespace Memory
} // namespace SushiRuntime