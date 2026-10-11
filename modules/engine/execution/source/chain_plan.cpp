/**************************************************************************/
/* chain_plan.cpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file chain_plan.cpp
 * @brief Classifies a compiled plan for the in-order chain and orders its nodes.
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /**
             * @brief Reports whether one node's kind and bindings allow it on the chain.
             * @param node A compiled, non-sentinel node.
             * @return False for native host work and for a node the installed coordinator
             *         marked as one a worker may run; the operation a node names does not
             *         decide.
             */
            bool node_kind_is_chainable(const Internal::Graph::Node& node) noexcept
            {
                using Mode = Internal::Scheduler::Task::ExecutionMode;
                const bool submits_device = node.execution_mode() == Mode::Kernel ||
                                            node.submits_device_work();
                if (!submits_device)
                    return false;

                // ExecutionEngine::prepare() ran first and set the flag, or no coordinator is installed.
                const Internal::Graph::OffloadState* offload = node.offload_state();
                return offload == nullptr || !offload->offloadable;
            }

            /**
             * @brief Resolves the one device queue every region of a node belongs to.
             * @param node A compiled, non-sentinel node.
             * @param context  The context whose registry owns the regions.
             * @return The queue index, or -1 for no region, an unknown region, or two devices.
             */
            int owning_queue_index(const Internal::Graph::Node& node,
                                   RuntimeContext& context) noexcept
            {
                int owner = -1;
                Internal::Memory::USMAllocator& memory = context.memory();
                auto visit = [&](const std::vector<Internal::Memory::ResourceRegion>& regions)
                {
                    for (const auto& r : regions)
                    {
                        if (!r.base)
                            continue;
                        const std::optional<std::size_t> device = memory.device_of(r.base);
                        const int index = device ? static_cast<int>(*device) : -1;
                        if (index < 0 || (owner >= 0 && index != owner))
                            return false;
                        owner = index;
                    }
                    return true;
                };
                if (!visit(node.io_writes_) || !visit(node.io_reads_))
                    return -1;
                return owner;
            }

            /**
             * @brief Orders every non-sentinel node so each follows all its predecessors.
             *
             * Kahn's algorithm over the compiled in-degree baseline, seeded in
             * all_nodes order, so one plan always yields the same order.
             * @param graph A plan after capture_base_state().
             * @return The order; shorter than the node count only if the plan has a cycle.
             */
            std::vector<Internal::Graph::Node*> topological_order(const Internal::Graph::CompiledGraph& graph)
            {
                const Internal::Graph::Node* sentinel = graph.sentinel.get();
                const std::size_t count = graph.all_nodes.size();

                std::vector<std::int32_t> remaining(graph.base_in_degree.begin(),
                                                    graph.base_in_degree.end());
                remaining.resize(count, 0);

                std::deque<Internal::Graph::Node*> ready;
                for (std::size_t i = 0; i < count; ++i)
                {
                    Internal::Graph::Node* node = graph.all_nodes[i].get();
                    if (node && node != sentinel && remaining[i] == 0)
                        ready.push_back(node);
                }

                std::vector<Internal::Graph::Node*> order;
                order.reserve(count);
                while (!ready.empty())
                {
                    Internal::Graph::Node* node = ready.front();
                    ready.pop_front();
                    order.push_back(node);
                    node->for_each_successor([&](Internal::Graph::Node* succ)
                    {
                        const std::size_t index = succ->plan_index;
                        if (succ == sentinel || index >= count || graph.all_nodes[index].get() != succ)
                            return;
                        if (--remaining[index] == 0)
                            ready.push_back(succ);
                    });
                }
                return order;
            }
        } // namespace

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         */
        ChainPlan classify_chain(const CompiledGraph& graph, RuntimeContext& context)
        {
            const Internal::Graph::Node* sentinel = graph.sentinel.get();
            std::size_t node_count = 0;
            int device = -1;

            for (const auto& owned : graph.all_nodes)
            {
                const Internal::Graph::Node* node = owned.get();
                if (!node || node == sentinel)
                    continue;
                ++node_count;

                if (!node_kind_is_chainable(*node))
                    return {};

                const int index = owning_queue_index(*node, context);
                if (index < 0 || (device >= 0 && index != device))
                    return {};
                device = index;
            }

            if (node_count == 0)
                return {};

            ChainPlan plan;
            plan.order = topological_order(graph);
            if (plan.order.size() != node_count)
                return {};

            plan.chainable = true;
            plan.queue_index = static_cast<std::size_t>(device);
            return plan;
        }

    } // namespace Execution
} // namespace SushiRuntime
