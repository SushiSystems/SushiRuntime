/**************************************************************************/
/* verify_memory_pool.cpp                                                 */
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

#include <cmath>
#include <vector>
#define NOMINMAX
#include <thread>
#include "../include/SushiRuntime/core/logger.hpp"
#include "../include/SushiRuntime/memory/usm_allocator.hpp"
#include "../include/SushiRuntime/execution/runtime_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Memory;

int main() 
{
    SR_LOGGER_SET_SYNC_MODE(true);
    SR_LOG_INFO("Starting memory pool verification...");

    try 
    {
        /** @brief Step 1: initialize the runtime context for computation. */
        SushiRuntime::Execution::RuntimeContext ctx;

        /** @brief Step 2: get the allocator for NUMA node 0. */
        auto allocator = ctx.get_allocator(0);

        /** @brief Step 3: allocate small objects. These should come from the bitmask pool. */
        SR_LOG_INFO("Allocating 10 small objects (256 bytes)...");
        std::vector<void*> small_ptrs;
        
        void* first_ptr = allocator->allocate(256);
        small_ptrs.push_back(first_ptr);

        for(int i=0; i<9; ++i) 
        {
            small_ptrs.push_back(allocator->allocate(256));
        }

        /** @brief Step 4: verify if allocations are contiguous (indicating pool usage). */
        long diff = static_cast<char*>(small_ptrs[1]) - static_cast<char*>(small_ptrs[0]);
        SR_LOG_INFO("Distance between first two allocations: {} bytes", diff);

        if (diff == 256) 
        {
            SR_LOG_INFO("Success: memory allocated contiguously (likely from pool).");
        }
        else 
        {
            SR_LOG_WARN("Memory not contiguous. Diff: {}", diff);
        }

        /** @brief Step 5: allocate a large object. This should bypass the pool and use system calls. */
        SR_LOG_INFO("Allocating large object (1MB)...");
        void* large_ptr = allocator->allocate(1024*1024);
        
        long large_diff = std::abs(static_cast<char*>(large_ptr) - static_cast<char*>(first_ptr));
        SR_LOG_INFO("Distance to large allocation: {} bytes", large_diff);

        if (large_diff > 16384) 
        {
            SR_LOG_INFO("Success: large object allocated outside pool page.");
        }

        /** @brief Step 6: clean up all allocated memory. */
        for(void* p : small_ptrs) allocator->deallocate(p);
        allocator->deallocate(large_ptr);

        SR_LOG_INFO("Verification completed.");
    }
    catch (const std::exception& e) 
    {
        SR_LOG_ERROR("Test failed: {}", e.what());
        return 1;
    }

    return 0;
}
