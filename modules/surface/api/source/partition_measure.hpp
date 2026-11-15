/**************************************************************************/
/* partition_measure.hpp                                                  */
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
 * @file partition_measure.hpp
 * @brief Declares the step drivers' calls that feed a partitioned plan's runs into its ledger and profile file.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        class GraphPartition;

        /**
         * @brief Adds a finished run of plan to the ledger of the graph's own partition session.
         *
         * The session takes the run only when the problem the graph was placed by is its plan in force,
         * and counts it as dropped otherwise; a timed run also hands over its dispatches.
         * @param partition The partition of the graph that ran; null for none.
         * @param seconds   Seconds the steps took on the engine's clock.
         */
        void measure_partitioned_run(const GraphPartition* partition, const Internal::Graph::CompiledGraph& plan,
                                     std::size_t steps, double seconds);

        /**
         * @brief Folds the ledger of the graph's session into the runtime's profile file, when one is set,
         *        then rebalances that session's plan; called at a horizon the graph's run passed.
         *
         * Only the graph whose run passed the horizon rebalances. A failed write is logged and the run goes on.
         * @param partition The partition of the graph that ran; null for none.
         * @see docs/design/GLOBAL_HANDLE/PARTITIONING.md
         */
        void fold_partition_ledger(Internal::Execution::RuntimeContext& context, const GraphPartition* partition);
    } // namespace Detail
} // namespace SushiRuntime
