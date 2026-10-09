/**************************************************************************/
/* ready_sink.hpp                                                         */
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
 * @file ready_sink.hpp
 * @brief Declares ReadySink, the buffer a finishing task reports unblocked work into.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <vector>

namespace SushiRuntime
{
    namespace Scheduler
    {
        class Task;

        /**
         * @brief Names the buffer where a finishing task reports the work it unblocked.
         * @see modules/engine/scheduler/README.md
         */
        using ReadySink = std::vector<Task*>;

    } // namespace Scheduler
} // namespace SushiRuntime
