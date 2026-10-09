/**************************************************************************/
/* scheduler_fixture.hpp                                                  */
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
 * @file scheduler_fixture.hpp
 * @brief Declares the helper that builds a TaskScheduler on the parts of a RuntimeContext.
 * @author Mustafa Garip
 * @see tests/common/README.md
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
        [[nodiscard]] inline Scheduler::TaskScheduler
        make_scheduler(Execution::RuntimeContext& context)
        {
            return Scheduler::TaskScheduler(Execution::worker_pool_settings(context.settings()),
                                            context.topology(), context.devices(),
                                            context.memory());
        }

    } // namespace Test
} // namespace SushiRuntime
