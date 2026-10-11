/**************************************************************************/
/* failure_naming.cpp                                                     */
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
 * @file failure_naming.cpp
 * @brief Names the task behind a failed run and builds the Error that carries the name.
 * @author Mustafa Garip
 */

#include "failure_naming.hpp"

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Names @p task for a failure report.
         * @return The task's label, with the registration index when it is a graph node.
         */
        std::string describe_task(const Task& task)
        {
            if (!task.is_graph_node())
                return Detail::sr_format("task '{}'", task.name());

            const auto& node = static_cast<const SushiRuntime::Internal::Graph::Node&>(task);
            return Detail::sr_format("node '{}' (registration index {})",
                                           node.name(), node.registration_index);
        }

        /**
         * @brief Builds the failure of @p task from an Error it returned, keeping its code.
         * @param task The task that failed.
         * @param cause The error a step of its dispatch returned.
         */
        Error name_failure(const Task& task, const Error& cause)
        {
            return Error{cause.code,
                         Detail::sr_format("{} failed: {}", describe_task(task), cause.message)};
        }

        /**
         * @brief Builds the KernelFailed failure of @p task from an exception it let out.
         * @param task The task that threw.
         * @param cause The exception it threw.
         */
        Error name_failure(const Task& task, const std::exception& cause)
        {
            return Error{ErrorCode::KernelFailed,
                         Detail::sr_format("{} failed: {}", describe_task(task), cause.what())};
        }

        /**
         * @brief Builds the KernelFailed failure of @p task from an object that is not a
         *        std::exception.
         * @param task The task that threw.
         */
        Error name_failure(const Task& task)
        {
            return Error{ErrorCode::KernelFailed,
                         Detail::sr_format(
                             "{} failed with an exception that is not a std::exception",
                             describe_task(task))};
        }

    } // namespace Scheduler
} // namespace SushiRuntime
