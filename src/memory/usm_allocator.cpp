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
                     /** @brief Initialize a small 16KB memory pool (64 slots * 256 bytes). */
                     size_t pool_size = 64 * 256;
                     void* raw = sycl::malloc_shared(pool_size, q);
                     
                     if (raw)
                     {
                        page_.start_ptr = raw;
                        page_.bitmask.store(0, std::memory_order_relaxed);
                        page_.numa_node_id = numa_id;
                        page_.slot_size = 256; 
                        pool_initialized_ = true;
                     }
                     else
                     {
                          SR_LOG_ERROR("Failed to allocate initial memory pool page.");
                          pool_initialized_ = false;
                     }
                }

                ~USMManagerImpl() override
                {
                    if(pool_initialized_ && page_.start_ptr) 
                    {
                        sycl::free(page_.start_ptr, queue_);
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
                    // TODO: optimize selection logic based on requested alignment.
                    if (pool_initialized_ && bytes <= 256 && strategy == AllocStrategy::SHARED)
                    {
                        void* p = pool_.allocate_from_page(page_);
                        if (p) return p;
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

                    /** @brief Return to pool if pointer is within the page range. */
                    if (pool_initialized_ && 
                        ptr >= page_.start_ptr && 
                        ptr < static_cast<char*>(page_.start_ptr) + (64 * 256))
                    {
                        pool_.deallocate_to_page(page_, ptr);
                        return;
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
                MemoryPage page_;
                bool pool_initialized_ = false;
        };

        /** @brief Factory function to create an instance of the USM manager. */
        sushi_ptr<USMAllocator> create_allocator(sycl::queue& q, int numa_id) 
        {
            return make_sushi<USMManagerImpl>(q, numa_id);
        }
        
    } // namespace Memory
} // namespace SushiRuntime