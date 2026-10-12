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
 * @file failure_naming.hpp
 * @brief Declares the functions that name the task behind a failed run.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Names a task for a failure report.
         * @return The task's label, with the registration index when it is a graph node.
         */
        [[nodiscard]] std::string describe_task(const Task& task);

        /**
         * @brief Builds the failure of a task from the Error its dispatch ended with.
         * @return An Error of the cause's code whose message opens with describe_task().
         */
        [[nodiscard]] Error name_failure(const Task& task, const Error& cause);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
