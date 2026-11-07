/**************************************************************************/
/* run_observation_fill.hpp                                               */
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
 * @file run_observation_fill.hpp
 * @brief Defines the two functions that fill a run's observation from its plan, for every engine.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <SushiRuntime/execution/dropped_dispatches.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/report/run_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /** @brief Empties @p observation but for the plan's table, for a run that records no timing. */
        inline void clear_observation(Internal::Graph::CompiledGraph& graph, RunObservation& observation)
        {
            observation.events.clear();
            observation.device_clocks.clear();
            observation.drops = DropCounts{};
            observation.table = graph.node_table;
            observation.graph_id = graph.graph_id;
        }

        /**
         * @brief Reads a plan's ledger into an observation and logs what the run lost.
         * @param observation Gains the table, the records, the pairings and the drop counts.
         */
        inline void fill_observation(Internal::Graph::CompiledGraph& graph, RunObservation& observation)
        {
            observation.events.clear();
            observation.device_clocks.clear();
            observation.table = graph.node_table;
            observation.graph_id = graph.graph_id;
            graph.profiling_ledger.collect(observation.events);
            graph.profiling_ledger.collect_pairings(observation.device_clocks);
            observation.drops = graph.profiling_ledger.drops();

            // One line per run for what the record lacks (modules/engine/execution/README.md).
            log_dropped_dispatches(observation.drops);
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
