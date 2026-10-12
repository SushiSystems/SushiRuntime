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
 * @brief Declares the runtime's size constants.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    /**
     * @brief Holds the cache line size in bytes, the alignment of per-worker and per-thread
     *        state.
     */
    inline constexpr std::size_t K_CACHE_LINE_SIZE = 64;

    /** @brief Holds the maximum number of supported NUMA nodes. */
    inline constexpr std::size_t K_MAX_NUMA_NODES = 64;

    /** @brief Holds the maximum number of worker threads. */
    inline constexpr std::size_t K_MAX_WORKER_THREADS = 256;

    /** @brief Holds the default memory alignment for USM (Unified Shared Memory). */
    inline constexpr std::size_t K_DEFAULT_ALIGNMENT = 128;
} // namespace SushiRuntime
