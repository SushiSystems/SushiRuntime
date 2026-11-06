/**************************************************************************/
/* run_failures.hpp                                                       */
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
 * @file run_failures.hpp
 * @brief Declares merge_run_failures, which picks the failure a run returns and logs the one it drops.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <optional>
#include <utility>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Returns the failure of a run: the task failure wins and the queue's is logged at WARN.
         * @param task_failure What the run's tasks recorded, or nothing.
         * @param queue_failure What the device queues held for their asynchronous handler.
         * @see modules/engine/execution/README.md
         */
        [[nodiscard]] inline Result<void> merge_run_failures(std::optional<Error> task_failure,
                                                             Result<void> queue_failure)
        {
            if (!task_failure)
                return queue_failure;

            if (!queue_failure)
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY,
                            "A device queue also reported an asynchronous error: {}; the task failure '{}' is returned.",
                            queue_failure.error().message, task_failure->message);
            return make_error(std::move(*task_failure));
        }
    } // namespace Execution
} // namespace SushiRuntime
