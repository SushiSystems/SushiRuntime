/**************************************************************************/
/* common.hpp                                                             */
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

#include <cstddef>
#include <cstdint>

namespace SushiRuntime 
{
    namespace Core
    {
        /**
         * @brief supported hardware backends.
         */
        enum class Backend 
        {
            SYCL, /**< sycl support via intel dpc++, adaptivecpp, or hipsycl. */
            CUDA, /**< native nvidia cuda support for interoperability. */
            HOST  /**< standard cpu host backend. */
        };

        /** @brief standard cache line size for modern hpc systems (in bytes). */
        inline constexpr size_t CACHE_LINE_SIZE = 64; 

        /** @brief maximum number of supported devices in numa or multi-gpu systems. */
        inline constexpr size_t MAX_SUPPORTED_XPU = 16;
        
        /** @brief maximum number of supported numa nodes. */
        inline constexpr size_t MAX_NUMA_NODES = 64;

        /** @brief default memory alignment for usm (unified shared memory). */
        inline constexpr size_t DEFAULT_ALIGNMENT = 128; 
        
    } // namespace Core
} // namespace SushiRuntime