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
 * @file ready_sink.hpp
 * @brief Declares ReadySink, the buffer a finishing task reports unblocked work into.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <vector>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        class Task;

        /**
         * @brief Names the buffer where a finishing task reports the work it unblocked.
         * @see modules/engine/scheduler/README.md
         */
        using ReadySink = std::vector<Task*>;

    } // namespace Scheduler
} // namespace SushiRuntime
