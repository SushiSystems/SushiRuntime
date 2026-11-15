/**************************************************************************/
/* region_coherence.hpp                                                   */
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
 * @file region_coherence.hpp
 * @brief Declares plan_region_coherence, the hook through which the composer and PlanCache add the planned copies of a region or a static graph.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <optional>
#include <vector>

#include <SushiRuntime/coherence/copy_plan.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Adds the allocate, copy and free nodes a region's nodes need on runtime's devices.
         *
         * A runtime with one device is left alone. GraphPartition places the nodes first; a cut
         * node is planned part by part. Host-visible homes are written back at the end.
         * @param nodes A built, stamped region or static graph, in insertion order; gains the planned nodes.
         * @return The spliced plan, nothing when no planner ran, or its error (AllocationFailed with shortfalls).
         */
        [[nodiscard]] Result<std::optional<Internal::Coherence::CopyPlan>>
        plan_region_coherence(std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                              Internal::Execution::RuntimeContext& runtime);
    } // namespace Detail
} // namespace SushiRuntime
