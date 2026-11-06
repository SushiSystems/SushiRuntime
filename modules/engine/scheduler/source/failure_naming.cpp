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
 * @file failure_naming.cpp
 * @brief Names the task behind a failed run and builds the Error that carries the name.
 * @author Mustafa Garip
 */

#include <SushiRuntime/scheduler/failure_naming.hpp>

#include <SushiLog/text/text_format.hpp>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        std::string describe_task(const Task& task)
        {
            if (!task.is_graph_node())
                return SushiLog::format_text("task '{}'", task.name());

            const auto& node = static_cast<const SushiRuntime::Internal::Graph::Node&>(task);
            return SushiLog::format_text("node '{}' (registration index {})",
                                         node.name(), node.registration_index);
        }

        Error name_failure(const Task& task, const Error& cause)
        {
            return Error{cause.code,
                         SushiLog::format_text("{} failed: {}", describe_task(task),
                                               cause.message)};
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
