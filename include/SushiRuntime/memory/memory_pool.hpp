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
         * 
         * Each uint64_t bitmask represents 64 slots in a page.
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
        };

        /**
         * @brief Thread-local cache (TLC) for fast allocation.
         * 
         * Allows each thread to access local slots without using locks.
         */
        struct ThreadLocalCache 
        {
            /** @brief Number of slots stored in a single batch. */
            static constexpr size_t BATCH_SIZE = 64;
            /** @brief Array of available memory slots. */
            void* slots[BATCH_SIZE];
            /** @brief Number of slots currently in the cache. */
            size_t count = 0;
            
            /** @brief Pops a slot from the local cache, or returns nullptr if empty. */
            inline void* pop() 
            {
                if (count == 0) return nullptr;
                return slots[--count];
            }
            
            /** @brief Pushes a free slot back to the local cache, returns false if full. */
            inline bool push(void* ptr) 
            {
                if (count >= BATCH_SIZE) return false;
                slots[count++] = ptr;
                return true;
            }
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
                 * @brief Batch allocates slots directly into a ThreadLocalCache.
                 * @param root_page Top of the linked pool.
                 * @param tlc The thread local cache object to fill.
                 * @return size_t How many slots were successfully grabbed.
                 */
                size_t allocate_batch(MemoryPage* root_page, ThreadLocalCache& tlc)
                {
                    size_t grabbed = 0;
                    while (grabbed < ThreadLocalCache::BATCH_SIZE)
                    {
                        void* ptr = allocate_from_pool(root_page);
                        if (!ptr) break; // Pool empty
                        tlc.slots[tlc.count++] = ptr;
                        grabbed++;
                    }
                    return grabbed;
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
