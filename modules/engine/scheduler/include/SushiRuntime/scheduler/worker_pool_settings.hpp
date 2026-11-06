/**************************************************************************/
/* worker_pool_settings.hpp                                               */
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
 * @file worker_pool_settings.hpp
 * @brief Declares WorkerPoolSettings, the worker pool a caller asks the scheduler for.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/machine/pin_policy.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Holds the worker pool requested from the scheduler at construction.
         * @see modules/engine/scheduler/README.md
         */
        struct WorkerPoolSettings
        {
            std::size_t worker_count = 0;  /**< Worker threads; 0 means one per logical core. */
            std::size_t first_core   = 0;  /**< First pinned core; ignored when floating. */
            PinPolicy   pinning      = PinPolicy::Pin;  /**< Whether workers are bound at all. */
        };

    } // namespace Scheduler
} // namespace SushiRuntime
