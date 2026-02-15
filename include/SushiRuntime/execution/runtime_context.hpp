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

#include <sycl/sycl.hpp>
#include <memory>
#include <vector>
#include <string>
#include "../core/common.hpp"
#include "../core/sushi_ptr.hpp"
#include "../memory/usm_allocator.hpp"

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief main control unit of SushiRuntime.
         * 
         * this class manages hardware, usm pools, and sycl queues.
         * it uses the pimpl pattern to keep the abi stable and hide logic.
         */
        class RuntimeContext 
        {
            public:
                /** @brief creates a new runtime context and scans for hardware. */
                RuntimeContext();
                
                /** @brief destroys the context and frees all resources. */
                ~RuntimeContext();

                /** @brief copy and move are disabled to keep state integrity. */
                RuntimeContext(const RuntimeContext&) = delete;
                RuntimeContext& operator=(const RuntimeContext&) = delete;

                /**
                 * @brief finds and lists all available hardware (cpu, gpu, fpga).
                 */
                void probe_hardware();

                /**
                 * @brief gets a sycl queue for a specific device.
                 * @param xpu_index the index of the device (default is 0).
                 * @return sycl::queue& reference to the device queue.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief selects a queue based on numa location.
                 * @param numa_id the id of the target numa node.
                 * @return sycl::queue& reference to the closest device queue.
                 */
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief returns the main usm allocator for this context.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> the global allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator();

                /**
                 * @brief returns a usm allocator optimized for a specific numa node.
                 * @param numa_id the id of the target numa node.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> the numa-optimized allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator(int numa_id);

                /**
                 * @brief waits for all async operations to finish (global sync).
                 */
                void wait_all();

            private:
                struct Impl;
                /** @brief pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_; 
        };
    
    } // namespace Execution
} // namespace SushiRuntime
