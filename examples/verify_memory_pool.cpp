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

#include "../include/SushiRuntime/execution/runtime_context.hpp"
#include "../include/SushiRuntime/memory/usm_allocator.hpp"
#include "../include/SushiRuntime/core/logger.hpp"
#include <iostream>
#include <vector>
#include <cmath>

using namespace SushiRuntime;
using namespace SushiRuntime::Memory;

int main() 
{
    SR_LOG_INFO("starting memory pool verification...");

    try 
    {
        /** @brief step 1: initialize the runtime context for computation. */
        SushiRuntime::Execution::RuntimeContext ctx;

        /** @brief step 2: get the allocator for numa node 0. */
        auto allocator = ctx.get_allocator(0);

        /** @brief step 3: allocate small objects. these should come from the bitmask pool. */
        SR_LOG_INFO("allocating 10 small objects (256 bytes)...");
        std::vector<void*> small_ptrs;
        
        void* first_ptr = allocator->allocate(256, AllocStrategy::SHARED);
        small_ptrs.push_back(first_ptr);

        for(int i=0; i<9; ++i) 
        {
            small_ptrs.push_back(allocator->allocate(256, AllocStrategy::SHARED));
        }

        /** @brief step 4: verify if allocations are contiguous (indicating pool usage). */
        long diff = static_cast<char*>(small_ptrs[1]) - static_cast<char*>(small_ptrs[0]);
        SR_LOG_INFO("distance between first two allocations: {} bytes", diff);

        if (diff == 256) 
        {
            SR_LOG_INFO("success: memory allocated contiguously (likely from pool).");
        }
        else 
        {
            SR_LOG_WARN("memory not contiguous. diff: {}", diff);
        }

        /** @brief step 5: allocate a large object. this should bypass the pool and use system calls. */
        SR_LOG_INFO("allocating large object (1mb)...");
        void* large_ptr = allocator->allocate(1024*1024, AllocStrategy::SHARED);
        
        long large_diff = std::abs(static_cast<char*>(large_ptr) - static_cast<char*>(first_ptr));
        SR_LOG_INFO("distance to large allocation: {} bytes", large_diff);

        if (large_diff > 16384) 
        {
            SR_LOG_INFO("success: large object allocated outside pool page.");
        }

        /** @brief step 6: clean up all allocated memory. */
        for(void* p : small_ptrs) allocator->deallocate(p);
        allocator->deallocate(large_ptr);

        SR_LOG_INFO("verification completed.");
    }
    catch (const std::exception& e) 
    {
        SR_LOG_ERROR("test failed: {}", e.what());
        return 1;
    }

    return 0;
}
