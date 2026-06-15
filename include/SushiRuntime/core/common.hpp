/**************************************************************************/
/* common.hpp                                                             */
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