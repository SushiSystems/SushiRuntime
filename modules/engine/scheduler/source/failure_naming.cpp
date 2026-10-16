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

#include <SushiRuntime/core/text_format.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        std::string describe_task(const Task& task)
        {
            if (!task.is_graph_node())
                return Detail::sr_format("task '{}'", task.name());

            const auto& node = static_cast<const SushiRuntime::Internal::Graph::Node&>(task);
            return Detail::sr_format("node '{}' (registration index {})",
                                           node.name(), node.registration_index);
        }

        Error name_failure(const Task& task, const Error& cause)
        {
            return Error{cause.code,
                         Detail::sr_format("{} failed: {}", describe_task(task), cause.message)};
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
