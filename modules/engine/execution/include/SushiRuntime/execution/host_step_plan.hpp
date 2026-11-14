/**************************************************************************/
/* host_step_plan.hpp                                                     */
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
 * @file host_step_plan.hpp
 * @brief Declares host_step_refusal(), which says whether a plan's steps can run on the host.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/host_reachable_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;

        /**
         * @brief Returns why the plan's steps cannot run on the calling thread, or null when
         *        they can.
         * @param memory Answers whether the host reaches each allocation a node names.
         * @return A string literal naming the first refusal, or null.
         * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
         */
        [[nodiscard]] SUSHIRUNTIME_API const char*
        host_step_refusal(const Internal::Graph::CompiledGraph& graph, const HostReachableMemory& memory);

        /**
         * @brief Returns why the plan's steps cannot run on the calling thread, reading the
         *        kind of each allocation from the context's allocator; null when they can.
         * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
         */
        [[nodiscard]] SUSHIRUNTIME_API const char*
        host_step_refusal(const Internal::Graph::CompiledGraph& graph, RuntimeContext& context);
    } // namespace Internal::Execution
} // namespace SushiRuntime
