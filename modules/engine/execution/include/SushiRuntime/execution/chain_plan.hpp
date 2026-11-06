/**************************************************************************/
/* chain_plan.hpp                                                         */
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
 * @file chain_plan.hpp
 * @brief Declares classify_chain, which fills the chain plan of a compiled graph.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/chain_plan.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/task/placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         * @param graph A plan after capture_base_state().
         * @param context The context whose USM registry owns the plan's regions.
         * @return The classification; not chainable on any doubt.
         * @see modules/engine/execution/README.md
         */
        SUSHIRUNTIME_API Internal::Graph::ChainPlan
        classify_chain(const Internal::Graph::CompiledGraph& graph, RuntimeContext& context);

        /**
         * @brief Classifies a compiled plan for the in-order chain, reading memory through a port.
         * @param graph A plan after capture_base_state().
         * @param memory Which device owns each region and how many devices there are.
         * @param has_coordinator Whether an offload coordinator is installed.
         * @see modules/engine/execution/README.md
         */
        SUSHIRUNTIME_API Internal::Graph::ChainPlan
        classify_chain(const Internal::Graph::CompiledGraph& graph, const Internal::Graph::PlacementMemory& memory,
                       bool has_coordinator);

    } // namespace Execution
} // namespace SushiRuntime
