/**************************************************************************/
/* pin_policy.hpp                                                         */
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
 * @file pin_policy.hpp
 * @brief Declares PinPolicy, the choice between bound and floating workers.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Selects whether workers are bound to specific cores or left to the OS.
         * @see modules/engine/execution/README.md
         */
        enum class PinPolicy
        {
            Pin,   /**< Bind worker i to core (first_core + i). */
            Float  /**< Leave placement to the OS scheduler. */
        };

    } // namespace Scheduler
} // namespace SushiRuntime
