/**************************************************************************/
/* common.hpp                                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file common.hpp
 * @brief Declares the Backend enumeration and the runtime's size constants.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    namespace Core
    {
        /** @brief Names the hardware backend a device or kernel targets. */
        enum class Backend
        {
            SYCL, /**< SYCL backend (Intel DPC++, AdaptiveCpp, or hipSYCL). */
            CUDA, /**< Unused. */
            HOST  /**< CPU host backend, no accelerator. */
        };

        /**
         * @brief Holds the cache line size in bytes, the alignment of per-worker and per-thread
         *        state.
         */
        inline constexpr size_t CACHE_LINE_SIZE = 64;

        /** @brief Holds the maximum number of supported devices in NUMA or multi-GPU systems. */
        inline constexpr size_t MAX_SUPPORTED_XPU = 16;

        /** @brief Holds the maximum number of supported NUMA nodes. */
        inline constexpr size_t MAX_NUMA_NODES = 64;

        /** @brief Holds the maximum number of worker threads. */
        inline constexpr size_t MAX_WORKER_THREADS = 256;

        /** @brief Holds the default memory alignment for USM (Unified Shared Memory). */
        inline constexpr size_t DEFAULT_ALIGNMENT = 128;

    } // namespace Core
} // namespace SushiRuntime

namespace sr = SushiRuntime;
