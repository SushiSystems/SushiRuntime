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
#include <set>
#include <utility>
#include <vector>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>
#include <SushiRuntime/coherence/plan_splice.hpp>
#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/coherence/usm_instance_allocator.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            /**
             * @brief Returns the buffers nodes touch whose home the host can read.
             *
             * The API does not track which buffers the host reads between two steps, so every
             * buffer with a host-visible home counts; one the host never reads costs a copy back.
             */
            std::set<GlobalHandle> host_read_buffers(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                                     const Internal::Memory::ResidencyView& directory)
            {
                std::set<GlobalHandle> touched;
                for (const sushi_ptr<Internal::Graph::Node>& node : nodes)
                {
                    for (const BufferRegion& region : node->io_reads_)
                        touched.insert(region.base);
                    for (const BufferRegion& region : node->io_writes_)
                        touched.insert(region.base);
                }
                std::set<GlobalHandle> read;
                for (GlobalHandle handle : touched)
                    if (Internal::Graph::home_is_host_visible(directory.inspect(handle), handle))
                        read.insert(handle);
                return read;
            }
        } // namespace

        Result<std::optional<Internal::Coherence::CopyPlan>>
        plan_region_coherence(std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                              Internal::Execution::RuntimeContext& runtime)
        {
            if (runtime.devices().count() < 2)
                return std::optional<Internal::Coherence::CopyPlan>();

            const Internal::Graph::USMPlacementMemory memory(runtime.memory(), runtime.handles());
            const Internal::Coherence::SpaceOfNode space_of =
                [&memory](const Internal::Graph::Node& node) -> std::optional<MemorySpaceID>
            {
                const Result<std::size_t> placed = Internal::Graph::placement_of(node, memory);
                if (!placed)
                    return std::nullopt;
                return Internal::Graph::device_memory_space(placed.value());
            };

            // The context owns the paths its devices support; a copy none of them makes is refused as Unsupported.
            const std::vector<Internal::Coherence::TransferPath*>& paths = runtime.transfer_paths();
            Internal::Coherence::CoherencePlanner planner(
                runtime.handles(), runtime.memory_spaces(),
                std::vector<const Internal::Coherence::TransferPath*>(paths.begin(), paths.end()),
                runtime.transfer_cost());
            // Planned instances stay resident; the context owns the policy and the cost.
            Internal::Coherence::ResidencyOptions residency;
            residency.policy = &runtime.eviction_policy();
            residency.host = Internal::Graph::host_memory_space();
            residency.host_read = host_read_buffers(nodes, runtime.handles());
            planner.set_residency(std::move(residency));
            Internal::Coherence::DirectoryPlannedNodeFactory factory(
                runtime.handles(),
                std::make_shared<Internal::Coherence::USMInstanceAllocator>(runtime.shared_memory()), paths);
            Result<Internal::Coherence::CopyPlan> spliced =
                Internal::Coherence::splice_plan(nodes, space_of, planner, factory,
                                                 &Internal::Graph::device_memory_space);
            if (!spliced)
                return make_error(std::move(spliced.error()));
            return std::optional<Internal::Coherence::CopyPlan>(std::move(spliced.value()));
        }
    } // namespace Detail
} // namespace SushiRuntime
