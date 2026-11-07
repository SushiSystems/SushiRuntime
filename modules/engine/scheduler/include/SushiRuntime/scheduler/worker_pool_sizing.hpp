/**************************************************************************/
/* worker_pool_sizing.hpp                                                */
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
 * @file worker_pool_sizing.hpp
 * @brief Declares size_worker_pool(), which turns the requested pool into the workers a machine runs.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /** @brief The worker count and first pinned core a pool runs with. */
        struct PoolSize
        {
            std::size_t workers = 1;    /**< Worker count, at least 1 and at most K_MAX_WORKER_THREADS. */
            std::size_t first_core = 0; /**< Core worker 0 is pinned to; meaningful when pinned. */
        };

        /**
         * @brief Sizes a pool: 0 workers means one per core, the count is capped, and a pinned core
         *        range past the last core is clamped; each correction logs one WARN line.
         * @param pool The pool asked for.
         * @param machine_cores Logical cores of the machine; 0 counts as 1.
         * @see modules/engine/scheduler/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API PoolSize size_worker_pool(const WorkerPoolSettings& pool,
                                                                 std::size_t machine_cores);
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
