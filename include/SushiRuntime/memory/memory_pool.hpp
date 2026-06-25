/**************************************************************************/
/* memory_pool.hpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <SushiRuntime/core/bit.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime 
{
    namespace Memory
    {
        using FlushCallback = void(*)(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count);
        // Exported: referenced by inline code (e.g. the magazine flush path) that
        // is compiled into consumer TUs, so the symbol must cross the DLL boundary
        // on Windows. SUSHIRUNTIME_API is a no-op where the lib is not a DLL.
        SUSHIRUNTIME_API extern std::atomic<FlushCallback> g_flush_callback;
        
        /**
         * @brief A page of fixed-size slots managed by the bitmask pool.
         *
         * Each MemoryPage belongs to exactly one ThreadPoolSlot at construction
         * time, indicated by owning_slot. This identity is permanent: a page
         * is only ever expanded into the slot that allocated it from SYCL,
         * and pages do not migrate between slots.
         *
         * The owning_slot field exists so that deallocate() can route a free
         * back to the slot that owns the page, even when the freeing thread
         * is not the slot owner. This enables the per-slot remote-free MPSC
         * mechanism that eliminates the "consumer magazine fills with foreign
         * pointers, triggers full-magazine flush spike" pathology.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) MemoryPage 
        {
            void* start_ptr;
            std::atomic<uint64_t> bitmask{0};
            std::atomic<MemoryPage*> next{nullptr};
            int numa_node_id;
            size_t slot_size;
            int bucket_idx;

            /**
             * @brief Identity of the ThreadPoolSlot that owns this page.
             *
             * Set once when the page is allocated by allocate_new_page() and
             * never changes thereafter. Used by deallocate() to dispatch
             * free operations to the correct slot's remote-free queue.
             */
            int owning_slot = -1;
        };

        /**
         * @brief Zero-atomic thread-local magazine with per-allocator banks.
         *
         * Operates wait-free and atomic-free in the hot path. Each USMAllocator
         * gets its own bank of cached slots, so interleaving allocations across
         * allocators (multi-GPU / multi-context) no longer flushes the whole
         * magazine on every switch — the old "flush cliff". A thread keeps up to
         * MAX_ALLOCATORS banks; touching one more evicts the least-recently-used
         * bank (flushing it back to its allocator). The common single-allocator
         * case stays on the fast path via the cur_bank_ cache: one comparison,
         * no scan.
         */
        struct alignas(SushiRuntime::Core::CACHE_LINE_SIZE) ThreadLocalMagazine
        {
            static constexpr size_t BATCH_SIZE = 256;
            static constexpr size_t NUM_BUCKETS = 4;

            /**
             * @brief Per-thread cap on simultaneously cached allocator banks.
             *
             * Covers typical heterogeneous nodes (a worker touches its NUMA-local
             * device, occasionally a sibling). A thread that interleaves more than
             * this many allocators evicts the LRU bank on each new one — correct,
             * just back to paying a flush. Sized to balance that against the
             * per-thread memory cost (each bank is NUM_BUCKETS * BATCH_SIZE ptrs).
             */
            static constexpr size_t MAX_ALLOCATORS = 4;

            /**
             * @brief Low-watermark threshold for proactive refill.
             *
             * When a bucket's count drops below this value, is_low() returns
             * true. Set to 25% of BATCH_SIZE: lower delays refill (more spikes),
             * higher refills too eagerly (wasted work).
             */
            static constexpr size_t LOW_WATERMARK = BATCH_SIZE / 4;

            /** @brief One allocator's cached slots; alloc_id 0 means the bank is unused. */
            struct Bank
            {
                uint64_t alloc_id = 0;
                uint64_t lru = 0;                       /**< Last-use tick, for eviction. */
                size_t count[NUM_BUCKETS] = {0};
                void* ptrs[NUM_BUCKETS][BATCH_SIZE];
            };

            Bank banks[MAX_ALLOCATORS];
            uint64_t tick_ = 0;          /**< Monotonic clock for LRU ordering. */
            Bank* cur_bank_ = nullptr;   /**< Last bank used; the single-allocator fast path. */

            /** @brief Flushes one bank's slots back to its allocator and empties it. */
            inline void flush_bank(Bank& b)
            {
                FlushCallback cb = g_flush_callback.load(std::memory_order_acquire);
                for (size_t i = 0; i < NUM_BUCKETS; ++i)
                {
                    if (b.count[i] > 0)
                    {
                        if (cb) cb(b.alloc_id, static_cast<int>(i), b.ptrs[i], b.count[i]);
                        b.count[i] = 0;
                    }
                }
            }

            /** @brief Flushes every bank; called on thread exit. */
            inline void flush_all()
            {
                for (auto& b : banks)
                    if (b.alloc_id != 0)
                        flush_bank(b);
            }

            /**
             * @brief Resolves the bank for @p alloc_id, allocating or evicting as needed.
             *
             * Fast path: the cached cur_bank_ when the id is unchanged. Otherwise
             * scans for a matching or empty bank; if all banks are in use, evicts
             * (flushes) the least-recently-used one. Owner-thread only — no locks.
             * @param alloc_id The allocator whose bank is requested.
             * @return Reference to the resolved bank.
             */
            inline Bank& bank_for(uint64_t alloc_id)
            {
                if (cur_bank_ && cur_bank_->alloc_id == alloc_id)
                    return *cur_bank_;

                Bank* victim = &banks[0];
                for (auto& b : banks)
                {
                    if (b.alloc_id == alloc_id)
                    {
                        b.lru = ++tick_;
                        cur_bank_ = &b;
                        return b;
                    }
                    if (b.alloc_id == 0)
                    {
                        b.alloc_id = alloc_id;
                        b.lru = ++tick_;
                        cur_bank_ = &b;
                        return b;
                    }
                    if (b.lru < victim->lru) victim = &b;
                }

                // All banks in use: evict the LRU back to its allocator, then reuse it.
                flush_bank(*victim);
                victim->alloc_id = alloc_id;
                victim->lru = ++tick_;
                cur_bank_ = victim;
                return *victim;
            }

            // HOT PATH: bank lookup (cached) + one decrement. No atomics, no locks.
            inline void* pop(int bucket_idx, uint64_t alloc_id)
            {
                Bank& b = bank_for(alloc_id);
                if (b.count[bucket_idx] == 0)
                    return nullptr;

                return b.ptrs[bucket_idx][--b.count[bucket_idx]];
            }

            // HOT PATH: bank lookup (cached) + one store. No atomics, no locks.
            inline bool push(int bucket_idx, void* ptr, uint64_t alloc_id)
            {
                Bank& b = bank_for(alloc_id);
                if (b.count[bucket_idx] >= BATCH_SIZE)
                    return false; // Magazine Full

                b.ptrs[bucket_idx][b.count[bucket_idx]++] = ptr;
                return true;
            }

            /**
             * @brief Returns true if a bucket is below the low-watermark.
             * @param bucket_idx The bucket to query.
             * @param alloc_id The allocator whose bank to query.
             * @return true if the bank's count for the bucket is below LOW_WATERMARK.
             */
            inline bool is_low(int bucket_idx, uint64_t alloc_id)
            {
                return bank_for(alloc_id).count[bucket_idx] < LOW_WATERMARK;
            }

            /**
             * @brief Returns the number of free slots in a bucket of an allocator's bank.
             *
             * Used by the allocator to size a bulk_claim_capped so it does not
             * grab more page slots than the magazine can hold.
             * @param bucket_idx The bucket to query.
             * @param alloc_id The allocator whose bank to query.
             * @return Slots that can still be pushed before the bank bucket is full.
             */
            inline size_t free_capacity(int bucket_idx, uint64_t alloc_id)
            {
                return BATCH_SIZE - bank_for(alloc_id).count[bucket_idx];
            }

            ~ThreadLocalMagazine() { flush_all(); }
        };

        inline thread_local ThreadLocalMagazine t_mag{};

        /**
         * @brief Lock-free slab allocator that tracks free slots with 64-bit bitmasks.
         *
         * Each MemoryPage holds 64 equally-sized slots; the bitmask marks allocated
         * bits. Allocation is a CAS on the bitmask — no mutex, no lock. The magazine
         * layer (ThreadLocalMagazine) amortizes CAS cost by batching claims in groups
         * of up to BATCH_SIZE.
         */
        class BitmaskMemoryPool
        {
            public:
                /**
                 * @brief Amortized Bulk Claim Strategy (TCMalloc style).
                 *
                 * Atomically claims ALL remaining free slots in a page using a
                 * single CAS, reducing atomic contention by a factor of 64.
                 *
                 * @warning Under high concurrency, calling this on a partially
                 *          free page means only one thread can succeed per
                 *          retry round. If multiple threads are simultaneously
                 *          refilling magazines, prefer bulk_claim_capped so
                 *          that each thread takes only what it needs and
                 *          leaves the rest for siblings.
                 *
                 * @param root_page Head of the page list for this bucket.
                 * @param out_array Output array for claimed pointers.
                 * @param max_claim Maximum pointers to write to out_array.
                 * @return Number of slots actually claimed (0 if exhausted).
                 */
                int bulk_claim(MemoryPage* root_page, void** out_array, int max_claim) 
                {
                    return bulk_claim_impl(root_page, out_array, max_claim, /*claim_all=*/true);
                }

                /**
                 * @brief Capped variant of bulk_claim that respects caller capacity.
                 *
                 * Differs from bulk_claim by claiming only up to max_claim
                 * slots per page, leaving any remaining free slots for other
                 * threads. This reduces CAS contention in the
                 * many-threads-refilling-simultaneously scenario by avoiding
                 * the "winner takes all" page-claim semantics of bulk_claim.
                 *
                 * Use this when refilling a magazine that has limited free
                 * capacity (e.g., the magazine is partially full and only
                 * needs a few pointers, not 64). Use bulk_claim instead when
                 * you genuinely want the entire page (cold start, dedicated
                 * thread refill, etc.).
                 *
                 * @param root_page Head of the page list for this bucket.
                 * @param out_array Output array for claimed pointers.
                 * @param max_claim Maximum pointers to claim. Should equal
                 *                  the magazine's free_capacity() to avoid
                 *                  wasting page slots.
                 * @return Number of slots actually claimed (0 if exhausted).
                 */
                int bulk_claim_capped(MemoryPage* root_page, void** out_array, int max_claim) 
                {
                    return bulk_claim_impl(root_page, out_array, max_claim, /*claim_all=*/false);
                }

                /**
                 * @brief Returns one slot to its page by clearing the corresponding bitmask bit.
                 * @param page Page that owns @p ptr (determined by the calling allocator).
                 * @param ptr  Slot pointer to free; must lie within @p page's slab.
                 */
                void deallocate_to_page(MemoryPage& page, void* ptr)
                {
                    size_t offset = static_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(page.start_ptr);
                    int slot_idx = static_cast<int>(offset / page.slot_size);
                    page.bitmask.fetch_and(~(1ULL << slot_idx), std::memory_order_release);
                }

            private:
                /**
                 * @brief Shared implementation for bulk_claim and bulk_claim_capped.
                 *
                 * When claim_all is true: claims every free slot in the page
                 * via a single CAS to ~0ULL (the original bulk_claim behavior).
                 *
                 * When claim_all is false: builds a target bitmask that flips
                 * only up to max_claim of the currently free bits, leaving
                 * the remainder for other threads. This is more contention-
                 * friendly under producer/consumer-style workloads where
                 * many threads refill simultaneously.
                 */
                int bulk_claim_impl(MemoryPage* root_page, void** out_array, 
                                    int max_claim, bool claim_all) 
                {
                    MemoryPage* current_page = root_page;
                    
                    while (current_page != nullptr)
                    {
                        uint64_t current_mask = current_page->bitmask.load(std::memory_order_relaxed);
                        
                        while (current_mask != ~0ULL)
                        { 
                            uint64_t free_bits = ~current_mask;
                            uint64_t target_mask;
                            int will_claim;

                            if (claim_all)
                            {
                                // Original behavior: take the whole page.
                                target_mask = ~0ULL;
                                will_claim = std::min(Core::popcount_u64(free_bits), max_claim);
                            }
                            else
                            {
                                // Capped: take only up to max_claim free bits.
                                int available = Core::popcount_u64(free_bits);
                                will_claim = std::min(available, max_claim);

                                // Build a mask of just the lowest will_claim free bits.
                                uint64_t take_bits = 0;
                                uint64_t remaining_free = free_bits;
                                for (int taken = 0; taken < will_claim; ++taken)
                                {
                                    int bit = Core::countr_zero_u64(remaining_free);
                                    take_bits |= (1ULL << bit);
                                    remaining_free &= ~(1ULL << bit);
                                }
                                target_mask = current_mask | take_bits;
                            }

                            if (current_page->bitmask.compare_exchange_weak(
                                    current_mask, target_mask, 
                                    std::memory_order_acquire, 
                                    std::memory_order_relaxed)) 
                            {
                                // We claimed exactly the bits in (target_mask & ~current_mask).
                                uint64_t claimed_bits = target_mask & ~current_mask;
                                int claimed_count = 0;
                                
                                for (int i = 0; i < 64 && claimed_count < will_claim; ++i) 
                                {
                                    if ((claimed_bits >> i) & 1) 
                                        out_array[claimed_count++] = 
                                            static_cast<uint8_t*>(current_page->start_ptr) 
                                            + (i * current_page->slot_size);
                                }
                                return claimed_count;
                            }
                            // CAS failed: current_mask was reloaded, loop and retry.
                        }

                        current_page = current_page->next.load(std::memory_order_acquire);
                    }

                    return 0; // Completely out of memory in this bucket
                }
        };

    } 
}