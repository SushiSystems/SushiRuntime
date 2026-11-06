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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
