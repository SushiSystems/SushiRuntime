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
         * @brief Supported hardware backends.
         */
        enum class Backend 
        {
            SYCL, /**< SYCL support via Intel DPC++, AdaptiveCPP, or hipSYCL. */
            CUDA, /**< Native NVIDIA CUDA support for interoperability. */
            HOST  /**< Standard CPU host backend. */
        };

        /** @brief Standard cache line size for modern HPC systems (in bytes). */
        inline constexpr size_t CACHE_LINE_SIZE = 64; 

        /** @brief Maximum number of supported devices in NUMA or multi-GPU systems. */
        inline constexpr size_t MAX_SUPPORTED_XPU = 16;
        
        /** @brief Maximum number of supported NUMA nodes. */
        inline constexpr size_t MAX_NUMA_NODES = 64;

        /** @brief Maximum number of worker threads. */
        inline constexpr size_t MAX_WORKER_THREADS = 256;

        /** @brief Default memory alignment for USM (Unified Shared Memory). */
        inline constexpr size_t DEFAULT_ALIGNMENT = 128; 
        
    } // namespace Core
} // namespace SushiRuntime

namespace sr = SushiRuntime;