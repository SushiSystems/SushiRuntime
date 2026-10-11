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
