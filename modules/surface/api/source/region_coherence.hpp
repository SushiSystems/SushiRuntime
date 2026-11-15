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
 * @brief Declares plan_region_coherence, the composer's hook that adds a region's planned copies.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <vector>

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
         * A runtime with one device is left alone. Each node runs where placement_of() puts it;
         * a node it cannot place is left to the dispatch to refuse.
         * @param nodes A built, stamped region; gains the planned nodes.
         * @return Nothing, or the error the planner gave; nodes is then unchanged.
         */
        [[nodiscard]] Result<void> plan_region_coherence(std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                                         Internal::Execution::RuntimeContext& runtime);
    } // namespace Detail
} // namespace SushiRuntime
