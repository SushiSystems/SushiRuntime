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
        using FlushCallback = void(*)(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count);
        extern std::atomic<FlushCallback> g_flush_callback;
        
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) MemoryPage 
        {
            void* start_ptr;
            std::atomic<uint64_t> bitmask{0};
            std::atomic<MemoryPage*> next{nullptr};
            int numa_node_id;
            size_t slot_size;
            int bucket_idx;
        };

        /**
         * @brief Zero-Atomic Thread Local Magazine.
         * * Operates completely wait-free and atomic-free in the hot path.
         * * Replaces single-fetch caching with a magazine model.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ThreadLocalMagazine 
        {
            static constexpr size_t BATCH_SIZE = 16384; 
            static constexpr size_t NUM_BUCKETS = 4;

            void* ptrs[NUM_BUCKETS][BATCH_SIZE];
            size_t count[NUM_BUCKETS] = {0};
            uint64_t current_alloc_id = 0;

            inline void flush_all() 
            {
                FlushCallback cb = g_flush_callback.load(std::memory_order_acquire);
                if (cb) 
                {
                    for (size_t i = 0; i < NUM_BUCKETS; ++i) 
                    {
                        if (count[i] > 0) 
                        {
                            cb(current_alloc_id, i, ptrs[i], count[i]);
                            count[i] = 0;
                        }
                    }
                }
            }

            inline void check_alloc_id(uint64_t alloc_id) 
            {
                if (current_alloc_id != alloc_id) 
                {
                    flush_all();
                    current_alloc_id = alloc_id;
                }
            }

            // HOT PATH: 2 CPU instructions. No atomics, no locks.
            inline void* pop(int bucket_idx, uint64_t alloc_id) 
            {
                check_alloc_id(alloc_id);

                if (count[bucket_idx] == 0) 
                    return nullptr;

                return ptrs[bucket_idx][--count[bucket_idx]];
            }

            // HOT PATH: 3 CPU instructions. No atomics, no locks.
            inline bool push(int bucket_idx, void* ptr, uint64_t alloc_id) 
            {
                check_alloc_id(alloc_id);
                if (count[bucket_idx] >= BATCH_SIZE) 
                    return false; // Magazine Full

                ptrs[bucket_idx][count[bucket_idx]++] = ptr;

                return true;
            }

            ~ThreadLocalMagazine() { flush_all(); }
        };

        inline thread_local ThreadLocalMagazine t_mag{};

        class BitmaskMemoryPool 
        {
            public:
                /**
                 * @brief Amortized Bulk Claim Strategy (TCMalloc style).
                 * Instantly claims ALL remaining free slots in a page using a single 
                 * atomic fetch_or/CAS, reducing atomic contention by a factor of 64.
                 */
                int bulk_claim(MemoryPage* root_page, void** out_array, int max_claim) 
                {
                    MemoryPage* current_page = root_page;
                    
                    while (current_page != nullptr)
                    {
                        uint64_t current_mask = current_page->bitmask.load(std::memory_order_relaxed);
                        
                        while (current_mask != ~0ULL)
                        { 
                            // Try to claim the entire page at once
                            uint64_t claimed_mask = ~0ULL; 
                            
                            if (current_page->bitmask.compare_exchange_weak(current_mask, claimed_mask, 
                                                                std::memory_order_acquire, 
                                                                std::memory_order_relaxed)) 
                            {
                                // We successfully claimed the difference. Find which bits we got.
                                uint64_t free_bits = ~current_mask;
                                int claimed_count = 0;
                                
                                for (int i = 0; i < 64 && claimed_count < max_claim; ++i) 
                                {
                                    if ((free_bits >> i) & 1) 
                                        out_array[claimed_count++] = static_cast<uint8_t*>(current_page->start_ptr) + (i * current_page->slot_size);
                                }
                                return claimed_count; // Returns batch size back to thread
                            }
                        }

                        current_page = current_page->next.load(std::memory_order_acquire);
                    }

                    return 0; // Completely out of memory in this bucket
                }

                void deallocate_to_page(MemoryPage& page, void* ptr) 
                {
                    size_t offset = static_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(page.start_ptr);
                    int slot_idx = static_cast<int>(offset / page.slot_size);
                    page.bitmask.fetch_and(~(1ULL << slot_idx), std::memory_order_release);
                }
        };

    } 
}