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
         * @brief Interface for memory allocation strategies (Strategy Pattern).
         * Resolves OCP violation by allowing open extension of allocation methods.
         */
        class SUSHIRUNTIME_API IAllocatorStrategy 
        {
        public:
            virtual ~IAllocatorStrategy() = default;
            virtual void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const = 0;
            virtual bool is_poolable() const = 0;
        };

        namespace Strategies 
        {
            class SUSHIRUNTIME_API Shared : public IAllocatorStrategy 
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override 
                {
                    return sycl::aligned_alloc_shared(alignment, bytes, queue);
                }
                
                bool is_poolable() const override   { return true; }
                static const Shared& instance()     { static Shared s; return s; }
            };

            class SUSHIRUNTIME_API Device : public IAllocatorStrategy 
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override {
                    return sycl::aligned_alloc_device(alignment, bytes, queue);
                }
                bool is_poolable() const override   { return false; }
                static const Device& instance()     { static Device s; return s; }
            };

            class SUSHIRUNTIME_API Host : public IAllocatorStrategy 
            {
            public:
                void* allocate_raw(size_t bytes, size_t alignment, sycl::queue& queue) const override {
                    return sycl::aligned_alloc_host(alignment, bytes, queue.get_context());
                }
                bool is_poolable() const override   { return false; }
                static const Host& instance()       { static Host s; return s; }
            };
        }

        /**
         * @brief Base class for USM (Unified Shared Memory) allocation.
         */
        class SUSHIRUNTIME_API USMAllocator : public Core::RefCounted
        {
            public:
                virtual ~USMAllocator() = default;

                /**
                 * @brief Allocates memory with specific alignment and strategy.
                 * @param bytes Number of bytes to allocate.
                 * @param strategy The allocation strategy to use.
                 * @param alignment Memory alignment requirements.
                 * @return void* Pointer to the allocated memory.
                 */
                virtual void* allocate(size_t bytes, 
                                    const IAllocatorStrategy& strategy = Strategies::Shared::instance(),
                                    size_t alignment = Core::DEFAULT_ALIGNMENT) = 0;

                /**
                 * @brief Frees allocated memory.
                 * @param ptr Pointer to the memory to free.
                 */
                virtual void deallocate(void* ptr) = 0;

                /**
                 * @brief Returns the device associated with a memory pointer.
                 * @param ptr Memory pointer to check.
                 * @return sycl::device The device that owns the memory.
                 */
                virtual sycl::device get_device_of(void* ptr) = 0;
        };

        /** @brief Helper function to create a new USM allocator. */
        SUSHIRUNTIME_API sushi_ptr<USMAllocator> create_allocator(sycl::queue& queue, int numa_id = 0);

    } // namespace Memory
} // namespace SushiRuntime