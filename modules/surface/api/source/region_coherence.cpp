/**************************************************************************/
/* region_coherence.cpp                                                   */
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
 * @file region_coherence.cpp
 * @brief Implements plan_region_coherence.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include "region_coherence.hpp"

#include <memory>
#include <optional>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>
#include <SushiRuntime/coherence/plan_splice.hpp>
#include <SushiRuntime/coherence/transfer_cost.hpp>
#include <SushiRuntime/coherence/usm_instance_allocator.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        Result<void> plan_region_coherence(std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                           Internal::Execution::RuntimeContext& runtime)
        {
            if (runtime.devices().count() < 2)
                return {};

            const Internal::Graph::USMPlacementMemory memory(runtime.memory(), runtime.handles());
            const Internal::Coherence::SpaceOfNode space_of =
                [&memory](const Internal::Graph::Node& node) -> std::optional<MemorySpaceID>
            {
                const Result<std::size_t> placed = Internal::Graph::placement_of(node, memory);
                if (!placed)
                    return std::nullopt;
                return Internal::Graph::device_memory_space(placed.value());
            };

            // No transfer path is registered yet; a plan that needs a copy is refused as Unsupported.
            Internal::Coherence::CoherencePlanner planner(runtime.handles(), runtime.memory_spaces(), {},
                                                          Internal::Coherence::uniform_transfer_cost());
            Internal::Coherence::DirectoryPlannedNodeFactory factory(
                runtime.handles(),
                std::make_shared<Internal::Coherence::USMInstanceAllocator>(runtime.shared_memory()), {});
            return Internal::Coherence::splice_plan(nodes, space_of, planner, factory);
        }
    } // namespace Detail
} // namespace SushiRuntime
