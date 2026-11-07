/**************************************************************************/
/* worker_statistics.hpp                                                  */
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
 * @file worker_statistics.hpp
 * @brief Declares WorkerStatistics, the counters of a worker agent.
 * @author Mustafa Garip
 * @see modules/extension/worker_types/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds a worker's counters since start().
         */
        struct WorkerStatistics
        {
            std::uint64_t tasks_run = 0;                      /**< Kernels that ran to the end. */
            std::uint64_t tasks_failed_kernel = 0;            /**< Kernels that threw. */
            std::uint64_t tasks_failed_output_allocation = 0; /**< Outputs that did not fit. */
            std::uint64_t tasks_failed_input_unavailable = 0; /**< Inputs that never arrived. */
            std::uint64_t tasks_failed_unknown_operation = 0; /**< Operations with no kernel. */
            std::uint64_t tasks_failed_shutting_down = 0;     /**< Tasks failed by stop(). */
            std::uint64_t cache_bytes = 0;                    /**< Bytes the cache holds. */
            std::uint64_t cache_evictions = 0;                /**< Buffers the cache dropped. */
            std::uint64_t registrations = 0;                  /**< WELCOME frames accepted. */
            std::uint64_t reconnects = 0;                     /**< Registrations after the first. */
        };

    } // namespace Distributed
} // namespace SushiRuntime
