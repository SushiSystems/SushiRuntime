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
#include "../../include/SushiRuntime/memory/usm_allocator.hpp"
#include "../../include/SushiRuntime/core/logger.hpp"
#include <atomic>
#include <bit>
#include <cstddef>

namespace SushiRuntime 
{
    namespace Memory
    {
        /**
         * @brief bitmask-based management for memory slots.
         * 
         * each uint64_t bitmask represents 64 slots in a page.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) MemoryPage 
        {
            /** @brief start address of the page. */
            void* start_ptr;
            /** @brief atomic mask where 0 means empty and 1 means full. */
            std::atomic<uint64_t> bitmask{0};
            /** @brief id of the numa node where this page lives. */
            int numa_node_id;
            /** @brief size of each slot in the page. */
            size_t slot_size;
        };

        /**
         * @brief thread-local cache (tlc) for fast allocation.
         * 
         * allows each thread to access local slots without using locks.
         */
        struct ThreadLocalCache 
        {
            /** @brief number of slots stored in a single batch. */
            static constexpr size_t BATCH_SIZE = 16;
            /** @brief array of available memory slots. */
            void* slots[BATCH_SIZE];
            /** @brief number of slots currently in the cache. */
            size_t count = 0;
        };

        /** @brief global thread-local storage for the cache. */
        inline thread_local ThreadLocalCache t_cache;

        /**
         * @brief memory pool that uses bitmasks to track free space.
         */
        class BitmaskMemoryPool 
        {
            public:
                /**
                 * @brief allocates a memory slot from a specific page.
                 * @param page the memory page to allocate from.
                 * @return void* pointer to the allocated memory, or nullptr if full.
                 */
                // TODO: support multiple pages to handle high allocation pressure.
                void* allocate_from_page(MemoryPage& page) 
                {
                    uint64_t current_mask = page.bitmask.load(std::memory_order_relaxed);
                    
                    while (current_mask != ~0ULL)
                    { 
                        // find the first zero bit (empty slot)
                        // TODO: use simd instructions (e.g. avx2) to speed up free slot search.
                        int first_free = std::countr_one(current_mask);
                        if (first_free >= 64) break;

                        uint64_t new_mask = current_mask | (1ULL << first_free);
                        
                        // use atomic swap to reserve the slot
                        if (page.bitmask.compare_exchange_weak(current_mask, new_mask, 
                                                            std::memory_order_release, 
                                                            std::memory_order_relaxed)) 
                        {
                            return static_cast<uint8_t*>(page.start_ptr) + (first_free * page.slot_size);
                        }
                    }
                    return nullptr;
                }

                /**
                 * @brief returns a memory slot back to the page.
                 * @param page the memory page that owns the slot.
                 * @param ptr the pointer to the memory being freed.
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
