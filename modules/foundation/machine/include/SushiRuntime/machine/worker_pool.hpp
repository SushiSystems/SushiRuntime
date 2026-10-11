/**************************************************************************/
/* worker_pool.hpp                                                        */
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
 * @file worker_pool.hpp
 * @brief Declares WorkerPool, the worker pool as the scheduler built it.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/machine/pin_policy.hpp>

namespace SushiRuntime
{
    /**
     * @brief Reports the worker pool as built, which may differ from the request.
     * @see modules/foundation/machine/README.md
     */
    struct WorkerPool
    {
        std::size_t count      = 0;   /**< Worker threads running. */
        std::size_t first_core = 0;   /**< First bound core; meaningless when floating. */
        PinPolicy   pinning    = PinPolicy::Pin;  /**< Whether workers are bound at all. */
    };
} // namespace SushiRuntime
