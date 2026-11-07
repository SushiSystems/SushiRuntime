/**************************************************************************/
/* worker_pool_sizing.cpp                                                */
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
 * @file worker_pool_sizing.cpp
 * @brief Sizes a worker pool from the requested settings and the machine's cores.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>

#include <cstddef>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        PoolSize size_worker_pool(const WorkerPoolSettings& pool, std::size_t machine_cores)
        {
            if (machine_cores == 0) machine_cores = 1;

            std::size_t threads = pool.worker_count != 0 ? pool.worker_count : machine_cores;
            if (threads > K_MAX_WORKER_THREADS)
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker count {} exceeds the maximum of {}; the pool runs {} workers.",
                            threads, K_MAX_WORKER_THREADS, K_MAX_WORKER_THREADS);
                threads = K_MAX_WORKER_THREADS;
            }
            if (threads == 0)
            {
                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker count resolved to 0; the pool runs 1 worker.");
                threads = 1;
            }

            PoolSize size;
            size.workers = threads;
            size.first_core = pool.first_core;

            // A core range past the last core is clamped and logged (modules/engine/scheduler/README.md).
            if (pool.pinning == PinPolicy::Pin && size.first_core + size.workers > machine_cores)
            {
                const std::size_t last = size.first_core + size.workers;
                size.first_core = size.first_core < machine_cores ? size.first_core : 0;
                if (size.first_core + size.workers > machine_cores)
                    size.workers = machine_cores > size.first_core ? machine_cores - size.first_core : 1;

                SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "Worker core range [{}, {}) exceeds the machine's {} cores; "
                            "clamped to {} workers starting at core {}.",
                            pool.first_core, last, machine_cores, size.workers, size.first_core);
            }
            return size;
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
