/**************************************************************************/
/* task_submission.hpp                                                    */
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
 * @file task_submission.hpp
 * @brief Declares the functions that submit one task to a queue behind its event dependencies.
 * @author Mustafa Garip
 */

#pragma once

#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        class Task;
    } // namespace Scheduler

    namespace Internal::Graph
    {
        struct SmallEventVector;

        /**
         * @brief Submits @p task to @p queue after @p dependencies and returns its event.
         * @param queue The queue the task submits to.
         * @param task The task to run.
         * @param dependencies The events the task waits on, or null for none.
         * @param handed_over Sink for the events a host body hands over, or null for no scope.
         * @return The task's event, or the error a host body returned.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<sycl::event> submit_task(sycl::queue& queue, Scheduler::Task& task,
                                                 const SmallEventVector* dependencies,
                                                 std::vector<sycl::event>* handed_over);

        /**
         * @brief Submits @p task to @p queue after @p dependencies and keeps no event for it.
         * @param queue The queue the task submits to.
         * @param task The task to run.
         * @param dependencies The events the task waits on, or null for none.
         * @return Nothing, or the error a host body returned.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<void> submit_task_discarding(sycl::queue& queue, Scheduler::Task& task,
                                                     const SmallEventVector* dependencies);

    } // namespace Graph
} // namespace SushiRuntime
