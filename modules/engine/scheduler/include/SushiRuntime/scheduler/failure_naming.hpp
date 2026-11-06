/**************************************************************************/
/* failure_naming.hpp                                                     */
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
 * @file failure_naming.hpp
 * @brief Declares the functions that name the task behind a failed run.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Names a task for a failure report.
         * @return The task's label, with the registration index when it is a graph node.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::string describe_task(const Task& task);

        /**
         * @brief Builds the failure of a task from the Error its dispatch ended with.
         * @return An Error of the cause's code whose message opens with describe_task().
         */
        [[nodiscard]] SUSHIRUNTIME_API Error name_failure(const Task& task, const Error& cause);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
