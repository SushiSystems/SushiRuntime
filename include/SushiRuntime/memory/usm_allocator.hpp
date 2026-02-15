/**************************************************************************/
/* usm_allocator.hpp                                                      */
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
#include <sycl/sycl.hpp>
#include "../core/common.hpp"
#include "../core/sushi_ptr.hpp"

namespace SushiRuntime
{
    namespace Memory
    {
        /** @brief strategies for memory allocation. */
        enum class AllocStrategy 
        {
            DEVICE, /**< gpu only for fast access. */
            SHARED, /**< shared between cpu and gpu for flexibility. */
            HOST    /**< cpu only using pinned memory. */
        };

        /**
         * @brief base class for usm (unified shared memory) allocation.
         */
        class USMAllocator : public Core::RefCounted
        {
            public:
                virtual ~USMAllocator() = default;

                /**
                 * @brief allocates memory with specific alignment and strategy.
                 * @param bytes number of bytes to allocate.
                 * @param strategy the allocation strategy to use.
                 * @param alignment memory alignment requirements.
                 * @return void* pointer to the allocated memory.
                 */
                virtual void* allocate(size_t bytes, 
                                    AllocStrategy strategy = AllocStrategy::SHARED,
                                    size_t alignment = Core::DEFAULT_ALIGNMENT) = 0;

                /**
                 * @brief frees allocated memory.
                 * @param ptr pointer to the memory to free.
                 */
                virtual void deallocate(void* ptr) = 0;

                /**
                 * @brief returns the device associated with a memory pointer.
                 * @param ptr memory pointer to check.
                 * @return sycl::device the device that owns the memory.
                 */
                virtual sycl::device get_device_of(void* ptr) = 0;
        };

        /** @brief helper function to create a new usm allocator. */
        sushi_ptr<USMAllocator> create_allocator(sycl::queue& queue, int numa_id = 0);

    } // namespace Memory
} // namespace SushiRuntime