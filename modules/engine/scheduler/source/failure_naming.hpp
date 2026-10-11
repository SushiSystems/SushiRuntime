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
 */

#pragma once

#include <exception>
#include <string>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Names @p task for a failure report.
         * @return The task's label, with the registration index when it is a graph node.
         */
        [[nodiscard]] std::string describe_task(const Task& task);

        /** @brief Builds the failure of @p task from an Error it returned, keeping its code. */
        [[nodiscard]] Error name_failure(const Task& task, const Error& cause);

        /** @brief Builds the KernelFailed failure of @p task from an exception it let out. */
        [[nodiscard]] Error name_failure(const Task& task, const std::exception& cause);

        /** @brief Builds the KernelFailed failure of @p task from an object that is not a std::exception. */
        [[nodiscard]] Error name_failure(const Task& task);

    } // namespace Scheduler
} // namespace SushiRuntime
