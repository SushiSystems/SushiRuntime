/**************************************************************************/
/* node_profiling.hpp                                                     */
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
 * @file node_profiling.hpp
 * @brief Declares NodeProfiling, the per-run profiling accumulators of a node.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Holds per-run profiling accumulators, summed across a run's steps.
         * @see modules/engine/graph/README.md
         */
        struct NodeProfiling
        {
            std::atomic<std::uint64_t> invocations{0};  /**< Dispatch count over the run. */
            std::atomic<std::uint64_t> device_ns{0};    /**< Summed kernel device time (ns). */
            std::atomic<std::uint64_t> host_ns{0};      /**< Summed native/host wall time (ns). */
        };

    } // namespace Graph
} // namespace SushiRuntime
