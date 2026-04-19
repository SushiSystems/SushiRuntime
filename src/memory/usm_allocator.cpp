/**************************************************************************/
/* usm_allocator.cpp                                                      */
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

#include <vector>
#include <algorithm>
#include <cstdint>
#include <sycl/sycl.hpp>
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/memory_pool.hpp>
#include <SushiRuntime/core/logger.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        using namespace SushiRuntime::Core;

        static std::atomic<uint64_t> g_allocator_id_counter{1};

        std::atomic<FlushCallback> g_flush_callback{nullptr};

        class USMManagerImpl;
        static std::atomic<USMManagerImpl*> g_allocator_registry[256];

        void global_flush_magazine(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count);

        /**
         * @brief Lock-Free 3-Level Radix Tree (PageMap) for O(1) pointer-to-page resolution.
         * Modeled after TCMalloc. Replaces all global locks and binary searches.
         * Maps a 50-bit virtual address space down to 4KB granular chunks.
         */
        class LockFreePageMap 
        {
        private:
            static constexpr size_t PAGE_SHIFT = 12; // 4KB Page Granularity
            
            // 3-Level Trie constraints covering 38 bits of PageID (50 bits of VMA)
            static constexpr size_t L1_BITS = 14; 
            static constexpr size_t L2_BITS = 12;
            static constexpr size_t L3_BITS = 12;

            static constexpr size_t L1_SIZE = 1ULL << L1_BITS; // 16384
            static constexpr size_t L2_SIZE = 1ULL << L2_BITS; // 4096
            static constexpr size_t L3_SIZE = 1ULL << L3_BITS; // 4096

            struct Leaf 
            {
                std::atomic<MemoryPage*> entries[L3_SIZE];
                Leaf() { for (auto& e : entries) e.store(nullptr, std::memory_order_relaxed); }
            };

            struct Node 
            {
                std::atomic<Leaf*> entries[L2_SIZE];
                Node() { for (auto& e : entries) e.store(nullptr, std::memory_order_relaxed); }
            };

            // Root array is statically allocated but highly cache-friendly (131 KB)
            std::atomic<Node*> root_[L1_SIZE];

        public:
            LockFreePageMap() 
            {
                for (auto& e : root_) e.store(nullptr, std::memory_order_relaxed);
            }

            ~LockFreePageMap() 
            {
                for (size_t i = 0; i < L1_SIZE; ++i) 
                {
                    Node* n = root_[i].load(std::memory_order_relaxed);
                    if (n) 
                    {
                        for (size_t j = 0; j < L2_SIZE; ++j) 
                        {
                            Leaf* l = n->entries[j].load(std::memory_order_relaxed);
                            if (l) delete l;
                        }
                        delete n;
                    }
                }
            }

            /**
             * @brief Registers a memory span into the Radix Tree.
             * Allocates trie nodes dynamically and lock-free via CAS.
             */
            void set(void* ptr, size_t size, MemoryPage* page) 
            {
                uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
                uintptr_t end = start + size;
                
                uintptr_t start_page = start >> PAGE_SHIFT;
                uintptr_t end_page = (end - 1) >> PAGE_SHIFT;

                for (uintptr_t p = start_page; p <= end_page; ++p) 
                {
                    uintptr_t i1 = (p >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
                    uintptr_t i2 = (p >> L3_BITS) & (L2_SIZE - 1);
                    uintptr_t i3 = p & (L3_SIZE - 1);

                    Node* node = root_[i1].load(std::memory_order_acquire);
                    if (!node) 
                    {
                        Node* new_node = new Node();
                        Node* expected = nullptr;
                        if (root_[i1].compare_exchange_strong(expected, new_node, 
                                                              std::memory_order_acq_rel, 
                                                              std::memory_order_acquire))
                            node = new_node; 
                        else 
                        {
                            delete new_node;
                            node = expected;
                        }
                    }

                    Leaf* leaf = node->entries[i2].load(std::memory_order_acquire);
                    if (!leaf) 
                    {
                        Leaf* new_leaf = new Leaf();
                        Leaf* expected = nullptr;
                        if (node->entries[i2].compare_exchange_strong(expected, new_leaf, 
                                                                      std::memory_order_acq_rel, 
                                                                      std::memory_order_acquire))
                            leaf = new_leaf;
                        else 
                        {
                            delete new_leaf;
                            leaf = expected;
                        }
                    }

                    // Map the exact 4KB chunk to our MemoryPage.
                    leaf->entries[i3].store(page, std::memory_order_release);
                }
            }

            /**
             * @brief Ultra-fast, wait-free, lock-free pointer resolution. O(1).
             */
            MemoryPage* get(void* ptr) const 
            {
                uintptr_t p = reinterpret_cast<uintptr_t>(ptr) >> PAGE_SHIFT;
                
                uintptr_t i1 = (p >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
                Node* node = root_[i1].load(std::memory_order_acquire);
                if (!node) return nullptr;

                uintptr_t i2 = (p >> L3_BITS) & (L2_SIZE - 1);
                Leaf* leaf = node->entries[i2].load(std::memory_order_acquire);
                if (!leaf) return nullptr;

                uintptr_t i3 = p & (L3_SIZE - 1);
                return leaf->entries[i3].load(std::memory_order_acquire);
            }
        };

        class USMManagerImpl : public USMAllocator 
        {
            public:
                explicit USMManagerImpl(sycl::queue& q, int numa_id) 
                    : queue_(q), numa_id_(numa_id), allocator_id_(g_allocator_id_counter.fetch_add(1, std::memory_order_relaxed)) 
                {
                    g_allocator_registry[allocator_id_ % 256].store(this, std::memory_order_release);
                    
                    FlushCallback expected = nullptr;
                    g_flush_callback.compare_exchange_strong(expected, global_flush_magazine);

                    for (int i = 0; i < NUM_BUCKETS; ++i) 
                    {
                        root_pages_[i].store(nullptr, std::memory_order_relaxed);
                        expansion_locks_[i].store(false, std::memory_order_relaxed);
                        allocate_new_page(i);
                    }
                }

                ~USMManagerImpl() override
                {
                    for (int i = 0; i < NUM_BUCKETS; ++i) 
                    {
                        g_allocator_registry[allocator_id_ % 256].store(nullptr, std::memory_order_release);

                        MemoryPage* curr = root_pages_[i].load(std::memory_order_relaxed);
                        while (curr)
                        {
                            MemoryPage* next = curr->next.load(std::memory_order_relaxed);
                            if (curr->start_ptr)
                                sycl::free(curr->start_ptr, queue_);

                            delete curr;
                            curr = next;
                        }
                    }
                }

                void flush_batch(int /*bucket_idx*/, void** ptrs, size_t count)
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        MemoryPage* owner = page_map_.get(ptrs[i]);
                        if (owner)
                            pool_.deallocate_to_page(*owner, ptrs[i]);
                    }
                }

                void* allocate(size_t bytes, 
                            const IAllocatorStrategy& strategy = Strategies::Shared::instance(),
                            size_t alignment = DEFAULT_ALIGNMENT) override 
                {
                    if (strategy.is_poolable() && bytes <= MAX_POOL_ALLOC_SIZE && alignment <= DEFAULT_ALIGNMENT)
                    {
                        int bucket_idx = get_bucket_index(bytes);

                        if (void* ptr = t_mag.pop(bucket_idx, allocator_id_)) 
                            return ptr;

                        
                        MemoryPage* root = root_pages_[bucket_idx].load(std::memory_order_acquire);
                        void* batch[64];
                        int claimed = pool_.bulk_claim(root, batch, 64);

                        if (claimed > 0)
                        {
                            // Put the remainder in the magazine for the next (claimed - 1) allocations
                            for (int i = 1; i < claimed; ++i) 
                                t_mag.push(bucket_idx, batch[i], allocator_id_);

                            return batch[0];
                        }

                        // 3. SLOW PATH: Allocate a new global page and try bulk claiming again
                        if (allocate_new_page(bucket_idx))
                        {
                            root = root_pages_[bucket_idx].load(std::memory_order_acquire);
                            claimed = pool_.bulk_claim(root, batch, 64);
                            
                            if (claimed > 0)
                            {
                                for (int i = 1; i < claimed; ++i) 
                                    t_mag.push(bucket_idx, batch[i], allocator_id_);

                                return batch[0];
                            }
                        }
                    }

                    void* ptr = nullptr;
                    try 
                    {
                        ptr = strategy.allocate_raw(bytes, alignment, queue_);
                    } 
                    catch (const sycl::exception& e) 
                    {
                        SR_LOG_ERROR("SYCL allocation failed: {}", e.what());
                    }
                    return ptr;
                }

                void deallocate(void* ptr) override 
                {
                    if (!ptr) return;

                    MemoryPage* owner = page_map_.get(ptr);
                    
                    if (owner)
                    {
                        if (t_mag.push(owner->bucket_idx, ptr, allocator_id_)) return;
                        
                        for (int i = 0; i < 64; ++i) 
                        {
                            void* flushed_ptr = t_mag.pop(owner->bucket_idx, allocator_id_);
                            MemoryPage* flushed_owner = page_map_.get(flushed_ptr);
                            pool_.deallocate_to_page(*flushed_owner, flushed_ptr);
                        }

                        t_mag.push(owner->bucket_idx, ptr, allocator_id_);
                        return;
                    }

                    sycl::free(ptr, queue_.get_context());
                }

                sycl::device get_device_of(void* ptr) override 
                {
                    return sycl::get_pointer_device(ptr, queue_.get_context());
                }

            private:
                sycl::queue& queue_;
                int numa_id_;
                uint64_t allocator_id_;
                BitmaskMemoryPool pool_;
                
                static constexpr int NUM_BUCKETS = 4;
                static constexpr size_t BUCKET_SIZES[NUM_BUCKETS] = {128, 256, 1024, 4096};
                static constexpr size_t MAX_POOL_ALLOC_SIZE = 4096;
                
                std::array<std::atomic<MemoryPage*>, NUM_BUCKETS> root_pages_;
                std::array<std::atomic<bool>, NUM_BUCKETS> expansion_locks_;

                // TCMalloc style Radix Tree replaces global mutex and sorted arrays
                LockFreePageMap page_map_;

                static constexpr size_t MAX_PAGES = 4096;
                std::atomic<MemoryPage*> global_pages_[MAX_PAGES] = {nullptr};
                std::atomic<int> global_page_count_{0};

                int get_bucket_index(size_t size)
                {
                    if (size <= 128) return 0;
                    if (size <= 256) return 1;
                    if (size <= 1024) return 2;
                    return 3;
                }

                bool allocate_new_page(int bucket_idx)
                {
                    size_t slot_size = BUCKET_SIZES[bucket_idx];
                    
                    bool expected = false;
                    while (!expansion_locks_[bucket_idx].compare_exchange_weak(expected, 
                                                                                true, 
                                                                                std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                        expected = false;
                    }
                    
                    size_t pool_size = 64 * slot_size;
                    void* raw = nullptr;
                    try 
                    {
                        raw = sycl::malloc_shared(pool_size, queue_);
                    } 
                    catch(...) {}

                    if (!raw)
                    {
                        SR_LOG_ERROR("Failed to allocate new memory pool page for bucket {}", bucket_idx);
                        expansion_locks_[bucket_idx].store(false, std::memory_order_release);
                        return false;
                    }

                    MemoryPage* new_page = new MemoryPage();
                    new_page->start_ptr = raw;
                    new_page->bitmask.store(0, std::memory_order_relaxed);
                    new_page->numa_node_id = numa_id_;
                    new_page->slot_size = slot_size;
                    new_page->bucket_idx = bucket_idx; 

                    MemoryPage* old_root = root_pages_[bucket_idx].load(std::memory_order_relaxed);
                    do 
                    {
                        new_page->next.store(old_root, std::memory_order_relaxed);
                    } 
                    while (!root_pages_[bucket_idx].compare_exchange_weak(old_root, new_page, 
                                                            std::memory_order_release, 
                                                            std::memory_order_relaxed));

                    int p_idx = global_page_count_.fetch_add(1, std::memory_order_relaxed);
                    if (static_cast<size_t>(p_idx) < MAX_PAGES) 
                        global_pages_[p_idx].store(new_page, std::memory_order_release);

                    // Register the newly allocated chunk in the Radix Tree
                    page_map_.set(raw, pool_size, new_page);

                    expansion_locks_[bucket_idx].store(false, std::memory_order_release);
                    return true;
                }
        };

        void global_flush_magazine(uint64_t alloc_id, int bucket_idx, void** ptrs, size_t count) 
        {
            USMManagerImpl* mgr = g_allocator_registry[alloc_id % 256].load(std::memory_order_acquire);
            if (mgr) mgr->flush_batch(bucket_idx, ptrs, count);
        }

        sushi_ptr<USMAllocator> create_allocator(sycl::queue& q, int numa_id) 
        {
            return make_sushi<USMManagerImpl>(q, numa_id);
        }
        
    } 
}