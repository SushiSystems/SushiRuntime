/**************************************************************************/
/* host_step_plan.cpp                                                     */
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
 * @file host_step_plan.cpp
 * @brief Defines host_step_refusal() and the USM adapter of HostReachableMemory.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#include <SushiRuntime/execution/host_step_plan.hpp>

#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_registry.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_state.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Answers HostReachableMemory from the kinds a USMAllocator recorded. */
            class USMHostReachableMemory final : public HostReachableMemory
            {
                public:
                    /** @brief Reads @p memory, which must outlive this object. */
                    explicit USMHostReachableMemory(const Internal::Memory::USMAllocator& memory) noexcept
                        : memory_(memory)
                    {

                    }

                    /** @brief Returns whether the allocation at @p base was made as host or shared USM. */
                    [[nodiscard]] bool host_reachable(const void* base) const override
                    {
                        const std::optional<Internal::Memory::USMRegistry::Record> record =
                            memory_.record_of(base);
                        if (!record || !record->kind)
                            return false;
                        return *record->kind == Internal::Memory::USMKind::Host ||
                               *record->kind == Internal::Memory::USMKind::Shared;
                    }

                private:
                    const Internal::Memory::USMAllocator& memory_; /**< The allocator whose records answer. */
            };

            /** @brief Returns whether every region of @p regions lies in memory the host reaches. */
            bool regions_reachable(const std::vector<BufferRegion>& regions, const HostReachableMemory& memory)
            {
                for (const BufferRegion& region : regions)
                    if (!memory.host_reachable(region.base))
                        return false;
                return true;
            }

            /** @brief Returns whether every address a live list holds lies in memory the host reaches. */
            bool addresses_reachable(const std::shared_ptr<std::vector<void*>>& addresses,
                                     const HostReachableMemory& memory)
            {
                if (addresses == nullptr)
                    return true;
                for (const void* address : *addresses)
                    if (address != nullptr && !memory.host_reachable(address))
                        return false;
                return true;
            }

            /** @brief Returns why one node cannot run on the host, or null when it can. */
            const char* node_refusal(const Internal::Graph::Node& node, const HostReachableMemory& memory)
            {
                const bool native = node.mode == Internal::Scheduler::Task::ExecutionMode::Host &&
                                    !node.submits_device_work_;
                // A native host node already runs on a worker thread of the host.
                if (native)
                    return nullptr;
                if (!node.host_form)
                    return "a node has no host form: an NdRange, a raw command group or a queue body";

                const Internal::Graph::OffloadState* offload = node.offload_state();
                if (offload != nullptr && (offload->offloadable || offload->payload != nullptr))
                    return "an offload coordinator acts on a node";
                if (node.io_reads_.empty() && node.io_writes_.empty())
                    return "a node names no region, so the memory its kernel touches is unknown";
                if (!regions_reachable(node.io_reads_, memory) || !regions_reachable(node.io_writes_, memory))
                    return "a node touches device memory or memory the runtime holds no kind for";
                if (offload != nullptr && (!addresses_reachable(offload->live_reads, memory) ||
                                           !addresses_reachable(offload->live_writes, memory)))
                    return "a node's live address lists hold device memory or memory the runtime holds no kind for";
                return nullptr;
            }
        } // namespace

        const char* host_step_refusal(const Internal::Graph::CompiledGraph& graph,
                                      const HostReachableMemory& memory)
        {
            if (graph.all_nodes.empty())
                return "the plan has no node";
            for (const auto& node : graph.all_nodes)
                if (const char* refusal = node_refusal(*node, memory))
                    return refusal;
            return nullptr;
        }

        const char* host_step_refusal(const Internal::Graph::CompiledGraph& graph, RuntimeContext& context)
        {
            return host_step_refusal(graph, USMHostReachableMemory(context.memory()));
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
