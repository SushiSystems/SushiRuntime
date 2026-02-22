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

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/memory_pool.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        using namespace SushiRuntime::Core;

        /**
         * @brief Implementation of the USM manager.
         * 
         * Handles small object allocations through a bitmask pool and larger ones via system calls.
         */
        class USMManagerImpl : public USMAllocator 
        {
            public:
                explicit USMManagerImpl(sycl::queue& q, int numa_id) : queue_(q), numa_id_(numa_id) 
                {
                     // Set up root page (not allocated here, but prepared)
                     root_page_.store(nullptr, std::memory_order_relaxed);
                     
                     // Pre-allocate the first page
                     allocate_new_page();
                }

                ~USMManagerImpl() override
                {
                    // Clean up all pages in the linked list
                    MemoryPage* curr = root_page_.load(std::memory_order_relaxed);
                    while (curr)
                    {
                        MemoryPage* next = curr->next.load(std::memory_order_relaxed);
                        if (curr->start_ptr)
                        {
                            sycl::free(curr->start_ptr, queue_);
                        }
                        delete curr;
                        curr = next;
                    }
                }

                /**
                 * @brief Allocates memory using bitmask pool or SYCL system calls.
                 * @param bytes Number of bytes requested.
                 * @param strategy The allocation strategy (device, shared, or host).
                 * @param alignment Alignment requirement for the pointer.
                 * @return void* Pointer to the allocated memory.
                 */
                void* allocate(size_t bytes, 
                            AllocStrategy strategy = AllocStrategy::SHARED,
                            size_t alignment = DEFAULT_ALIGNMENT) override 
                {
                    /** @brief Try to use the bitmask pool for small shared allocations. */
                    if (bytes <= 256 && strategy == AllocStrategy::SHARED)
                    {
                        void* ptr = t_cache.pop();
                        if (ptr) return ptr;
                        
                        // Local cache is empty, fetch a batch from the global pool
                        MemoryPage* root = root_page_.load(std::memory_order_acquire);
                        if (root)
                        {
                            size_t grabbed = pool_.allocate_batch(root, t_cache);
                            if (grabbed == 0)
                            {
                                // Pool is entirely full, dynamically expand!
                                if (allocate_new_page())
                                {
                                    // Successfully added a new page, try fetching again
                                    root = root_page_.load(std::memory_order_acquire);
                                    pool_.allocate_batch(root, t_cache);
                                }
                            }
                            
                            ptr = t_cache.pop();
                            if (ptr) return ptr;
                        }
                    }

                    /** @brief Fall back to system allocation if pool is full or request is large. */
                    // TODO: implement a thread-local cache for frequently reused small blocks.
                    // TODO: create a dedicated arena allocator for large, infrequent requests.
                    void* ptr = nullptr;
                    try 
                    {
                        if (strategy == AllocStrategy::SHARED) 
                        {
                            ptr = sycl::aligned_alloc_shared(alignment, bytes, queue_);
                        } 
                        else if (strategy == AllocStrategy::DEVICE) 
                        {
                            ptr = sycl::aligned_alloc_device(alignment, bytes, queue_);
                        } 
                        else 
                        {
                            ptr = sycl::aligned_alloc_host(alignment, bytes, queue_.get_context());
                        }
                    } 
                    catch (const sycl::exception& e) 
                    {
                        SR_LOG_ERROR("SYCL allocation failed: {}", e.what());
                    }

                    return ptr;
                }

                /**
                 * @brief Deallocates memory. Automatically detects if it belongs to the pool.
                 * @param ptr Pointer to the memory to return.
                 */
                void deallocate(void* ptr) override 
                {
                    if (!ptr) return;

                    /** @brief Return to pool if pointer belongs to any page. */
                    if (strategy_is_shared_pool(ptr))
                    {
                        // Try to push straight to thread local cache first for massive speedup
                        if (t_cache.push(ptr)) return;
                        
                        // If local cache is full, return directly to its physical global page
                        MemoryPage* owner = find_owner_page(ptr);
                        if (owner)
                        {
                            pool_.deallocate_to_page(*owner, ptr);
                            return;
                        }
                    }

                    sycl::free(ptr, queue_.get_context());
                }

                /** @brief Retrieves the SYCL device that owns the memory pointer. */
                sycl::device get_device_of(void* ptr) override 
                {
                    return sycl::get_pointer_device(ptr, queue_.get_context());
                }

            private:
                sycl::queue& queue_;
                int numa_id_;
                BitmaskMemoryPool pool_;
                std::atomic<MemoryPage*> root_page_{nullptr};
                
                /** @brief Protects page expansion */
                std::atomic_flag expansion_lock_ = ATOMIC_FLAG_INIT;

                bool allocate_new_page()
                {
                    // Only one thread should be expanding the pool at a time
                    if (expansion_lock_.test_and_set(std::memory_order_acquire))
                    {
                        // Another thread is already expanding it. Yield and let them finish.
                        std::this_thread::yield();
                        return false; 
                    }
                    
                    size_t pool_size = 64 * 256;
                    void* raw = nullptr;
                    try {
                        raw = sycl::malloc_shared(pool_size, queue_);
                    } catch(...) {}

                    if (!raw)
                    {
                        SR_LOG_ERROR("Failed to allocate new memory pool page.");
                        expansion_lock_.clear(std::memory_order_release);
                        return false;
                    }

                    MemoryPage* new_page = new MemoryPage();
                    new_page->start_ptr = raw;
                    new_page->bitmask.store(0, std::memory_order_relaxed);
                    new_page->numa_node_id = numa_id_;
                    new_page->slot_size = 256;
                    // new_page->next is already nullptr

                    // Thread it to the front of the list (Lock-Free Push)
                    MemoryPage* old_root = root_page_.load(std::memory_order_relaxed);
                    do {
                        new_page->next.store(old_root, std::memory_order_relaxed);
                    } while (!root_page_.compare_exchange_weak(old_root, new_page, 
                                                            std::memory_order_release, 
                                                            std::memory_order_relaxed));

                    SR_LOG_INFO("BitmaskMemoryPool expanded with a new page.");
                    expansion_lock_.clear(std::memory_order_release);
                    return true;
                }

                bool strategy_is_shared_pool(void* ptr)
                {
                    MemoryPage* curr = root_page_.load(std::memory_order_acquire);
                    while (curr)
                    {
                        void* start = curr->start_ptr;
                        void* end = static_cast<char*>(start) + (64 * 256);
                        if (ptr >= start && ptr < end) return true;
                        curr = curr->next.load(std::memory_order_acquire);
                    }
                    return false;
                }
                
                MemoryPage* find_owner_page(void* ptr)
                {
                    MemoryPage* curr = root_page_.load(std::memory_order_acquire);
                    while (curr)
                    {
                        void* start = curr->start_ptr;
                        void* end = static_cast<char*>(start) + (64 * 256);
                        if (ptr >= start && ptr < end) return curr;
                        curr = curr->next.load(std::memory_order_acquire);
                    }
                    return nullptr;
                }
        };

        /** @brief Factory function to create an instance of the USM manager. */
        sushi_ptr<USMAllocator> create_allocator(sycl::queue& q, int numa_id) 
        {
            return make_sushi<USMManagerImpl>(q, numa_id);
        }
        
    } // namespace Memory
} // namespace SushiRuntime