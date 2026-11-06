/**************************************************************************/
/* scheduler_factory.hpp                                                  */
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
 * @file scheduler_factory.hpp
 * @brief Declares the helper that builds a TaskScheduler on the parts of a RuntimeContext.
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Builds a scheduler from a context's settings, topology, devices and allocator.
         * @param context Must outlive the returned scheduler.
         * @return The scheduler, constructed in the caller's storage; no thread is started.
         */
        [[nodiscard]] inline Internal::Scheduler::TaskScheduler
        make_scheduler(Internal::Execution::RuntimeContext& context)
        {
            return Internal::Scheduler::TaskScheduler(Internal::Execution::worker_pool_settings(context.settings()),
                                            context.topology(), context.devices(),
                                            context.memory());
        }
    } // namespace Test
} // namespace SushiRuntime
