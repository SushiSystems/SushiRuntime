/**************************************************************************/
/* runtime_context.hpp                                                    */
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

#include <memory>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief Main control unit of SushiRuntime.
         * 
         * This class manages hardware, USM pools, and SYCL queues.
         * It uses the Pimpl pattern to keep the ABI stable and hide logic.
         */
        class SUSHIRUNTIME_API RuntimeContext 
        {
            public:
                /** @brief Creates a new runtime context and scans for hardware. */
                RuntimeContext();
                
                /** @brief Destroys the context and frees all resources. */
                ~RuntimeContext();

                /** @brief Copy and move are disabled to keep state integrity. */
                RuntimeContext(const RuntimeContext&) = delete;
                RuntimeContext& operator=(const RuntimeContext&) = delete;

                /**
                 * @brief Finds and lists all available hardware (CPU, GPU, FPGA).
                 */
                void probe_hardware();

                /**
                 * @brief Gets a SYCL queue for a specific device.
                 * @param xpu_index The index of the device (default is 0).
                 * @return sycl::queue& Reference to the device queue.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Selects a queue based on NUMA location.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sycl::queue& Reference to the closest device queue.
                 */
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief Returns the main USM allocator for this context.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The global allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator();

                /**
                 * @brief Returns a USM allocator optimized for a specific NUMA node.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The NUMA-optimized allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator(int numa_id);

                /**
                 * @brief Waits for all async operations to finish (global sync).
                 */
                void wait_all();

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_; 
        };
    
    } // namespace Execution
} // namespace SushiRuntime
