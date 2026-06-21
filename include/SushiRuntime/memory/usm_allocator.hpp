/**************************************************************************/
/* usm_allocator.hpp                                                      */
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

#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Policy interface for selecting a SYCL USM allocation kind.
         *
         * Injected into USMAllocator so the allocation kind (shared, device, host)
         * can be swapped without changing the allocator. Stateless; implementations
         * expose a singleton via instance().
         */
        class SUSHIRUNTIME_API IAllocatorStrategy
        {
        public:
            virtual ~IAllocatorStrategy() = default;

            /**
             * @brief Issues a raw aligned SYCL USM allocation.
             * @param bytes     Number of bytes to allocate.
             * @param alignment Required byte alignment.
             * @param queue     Queue whose device and context own the allocation.
             * @return USM pointer on success, null on failure.
             */
            virtual void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const = 0;

            /**
             * @brief Whether this strategy's allocations may be pooled in BitmaskMemoryPool.
             * @return true for shared USM (host-addressable and pool-compatible); false otherwise.
             */
            virtual bool is_poolable() const = 0;
        };

        namespace Strategies
        {
            /** @brief Allocates SYCL shared USM (host + device accessible). Poolable. */
            class SUSHIRUNTIME_API Shared : public IAllocatorStrategy
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override
                {
                    return sycl::aligned_alloc_shared(alignment, bytes, queue);
                }

                bool is_poolable() const override { return true; }
                static const Shared& instance()   { static Shared s; return s; }
            };

            /** @brief Allocates SYCL device USM (device-only). Not poolable. */
            class SUSHIRUNTIME_API Device : public IAllocatorStrategy
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override
                {
                    return sycl::aligned_alloc_device(alignment, bytes, queue);
                }

                bool is_poolable() const override { return false; }
                static const Device& instance()   { static Device s; return s; }
            };

            /** @brief Allocates SYCL host USM (host-pinned, device-accessible). Not poolable. */
            class SUSHIRUNTIME_API Host : public IAllocatorStrategy
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override
                {
                    return sycl::aligned_alloc_host(alignment, bytes, queue.get_context());
                }

                bool is_poolable() const override { return false; }
                static const Host& instance()     { static Host s; return s; }
            };
        }

        /**
         * @brief Abstract SYCL USM allocator with pluggable allocation strategy.
         *
         * Concrete implementations (create_allocator()) wrap a BitmaskMemoryPool and
         * a SYCL queue; they track ownership so free_usm() can route each pointer
         * back to its context. RefCounted so sushi_ptr manages its lifetime.
         */
        class SUSHIRUNTIME_API USMAllocator : public Core::RefCounted
        {
            public:
                virtual ~USMAllocator() = default;

                /**
                 * @brief Allocates USM with the given strategy and alignment.
                 * @param bytes     Number of bytes to allocate.
                 * @param strategy  Allocation kind (shared by default).
                 * @param alignment Byte alignment; must be a power of two.
                 * @return USM pointer on success, null on failure.
                 */
                virtual void* allocate(size_t bytes,
                                       const IAllocatorStrategy& strategy = Strategies::Shared::instance(),
                                       size_t alignment = Core::DEFAULT_ALIGNMENT) = 0;

                /**
                 * @brief Frees a USM pointer obtained from allocate().
                 * @param ptr Pointer to free; must have been returned by this allocator.
                 */
                virtual void deallocate(void* ptr) = 0;

                /**
                 * @brief Returns the SYCL device that owns a USM allocation.
                 * @param ptr A USM pointer allocated by this allocator.
                 * @return The owning device, used by the scheduler to route kernel submissions.
                 */
                virtual sycl::device get_device_of(void* ptr) = 0;
        };

        /** @brief Helper function to create a new USM allocator. */
        SUSHIRUNTIME_API sushi_ptr<USMAllocator> create_allocator(sycl::queue& queue, int numa_id = 0);

    } // namespace Memory
} // namespace SushiRuntime