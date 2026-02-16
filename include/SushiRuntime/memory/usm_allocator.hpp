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
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        /** @brief Strategies for memory allocation. */
        enum class AllocStrategy 
        {
            DEVICE, /**< GPU only for fast access. */
            SHARED, /**< Shared between CPU and GPU for flexibility. */
            HOST    /**< CPU only using pinned memory. */
        };

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
                                    AllocStrategy strategy = AllocStrategy::SHARED,
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