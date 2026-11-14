/**************************************************************************/
/* run_description_builder.hpp                                            */
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
 * @file run_description_builder.hpp
 * @brief Defines describe_run, which measures a plan for a run placement policy.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/placement/run_description.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns what a policy reads of a run of @p plan that asks for @p steps steps.
         * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
         */
        inline RunDescription describe_run(const Internal::Graph::CompiledGraph& plan, std::size_t steps)
        {
            RunDescription run;
            run.node_count = plan.all_nodes.size();
            run.steps = steps;
            for (const auto& node : plan.all_nodes)
            {
                const Internal::Graph::NodeWidths widths = Internal::Graph::snapshot_widths(*node);
                if (widths.launched == K_WIDTH_UNKNOWN)
                    ++run.unsized_nodes;
                else
                    run.launched_items += widths.launched;

                if (node->metadata.cost)
                {
                    ++run.costed_nodes;
                    run.declared_flops += node->metadata.cost->flops;
                    run.declared_bytes += node->metadata.cost->bytes;
                }
            }
            return run;
        }
    } // namespace Detail
} // namespace SushiRuntime
