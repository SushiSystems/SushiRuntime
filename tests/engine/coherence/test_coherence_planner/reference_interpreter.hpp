/**************************************************************************/
/* reference_interpreter.hpp                                              */
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
 * @file reference_interpreter.hpp
 * @brief Declares run_reference, the sequential interpreter that keeps one copy of each buffer.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include "graph_case.hpp"
#include "node_operation.hpp"

namespace SushiRuntime
{
    namespace Internal::Coherence::Equivalence
    {
        /**
         * @brief Runs the nodes of graph in order over one host copy of each buffer.
         *
         * There is no device, no copy and no plan, so what it leaves is what any placement must give.
         */
        [[nodiscard]] inline Outcome run_reference(const GraphCase& graph)
        {
            Outcome outcome;
            outcome.buffers = graph.initial;
            const BufferAddress address = [&outcome](std::size_t buffer)
            {
                return outcome.buffers[buffer].data();
            };
            for (const GraphNode& node : graph.nodes)
                outcome.digests.push_back(run_node(node, address));
            return outcome;
        }
    } // namespace Internal::Coherence::Equivalence
} // namespace SushiRuntime
