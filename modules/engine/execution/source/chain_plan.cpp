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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>
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
             * @brief Reports whether the installed coordinator acts on a node when it runs.
             * @param node A compiled, non-sentinel node that ExecutionEngine::prepare() has seen.
             * @return True for a node a worker may run and for a node whose local completion
             *         the coordinator records: one with stored handles or with live write lists.
             * @see modules/engine/execution/README.md
             */
            bool is_coordinator_concern(const Internal::Graph::Node& node) noexcept
            {
                const Internal::Graph::OffloadState* offload = node.offload_state();
                return offload != nullptr && (offload->offloadable || offload->payload != nullptr ||
                                              offload->live_writes != nullptr);
            }

            /**
             * @brief Reports whether one node's kind and bindings allow it on the chain.
             * @param node A compiled, non-sentinel node.
             * @param has_coordinator Whether the context has an offload coordinator installed.
             * @return False for native host work and, with a coordinator installed, for a node
             *         it acts on; the operation a node names does not decide.
             */
            bool node_kind_is_chainable(const Internal::Graph::Node& node,
                                        bool has_coordinator) noexcept
            {
                using Mode = Internal::Scheduler::Task::ExecutionMode;
                const bool submits_device = node.execution_mode() == Mode::Kernel ||
                                            node.submits_device_work();
                if (!submits_device)
                    return false;

                // Only the scheduler's poller calls OffloadCoordinator::on_local_completion().
                return !has_coordinator || !is_coordinator_concern(node);
            }

            /**
             * @brief Resolves the device a node runs on when every region of it belongs to one
             *        device: placement_of(), so a stated device wins or is refused as at dispatch.
             * @param node A compiled, non-sentinel node.
             * @param context  The context whose registry owns the regions.
             * @return The queue index, or -1 for no region, an unknown region, two devices, or a
             *         placement placement_of() refuses, which the scheduler then reports.
             */
            int chained_queue_index(const Internal::Graph::Node& node, RuntimeContext& context)
            {
                int owner = -1;
                Internal::Memory::USMAllocator& memory = context.memory();
                auto visit = [&](const std::vector<BufferRegion>& regions)
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
                if (!visit(node.io_writes_) || !visit(node.io_reads_) || owner < 0)
                    return -1;

                const Result<std::size_t> placed = Internal::Graph::placement_of(node, memory);
                if (!placed || placed.value() >= context.devices().count())
                    return -1;
                return static_cast<int>(placed.value());
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
            /**
             * @brief Reports whether the chain would serialise two host bodies the plan leaves
             *        unordered: it holds a HOST node and some node of @p order is not a
             *        successor of the node before it.
             * @param order A topological order of every non-sentinel node of the plan.
             */
            bool orders_unordered_host_work(const std::vector<Internal::Graph::Node*>& order)
            {
                using Mode = Internal::Scheduler::Task::ExecutionMode;
                const bool has_host = std::any_of(order.begin(), order.end(),
                                                  [](const Internal::Graph::Node* node)
                                                  {
                                                      return node->execution_mode() == Mode::Host;
                                                  });
                if (!has_host)
                    return false;

                for (std::size_t position = 1; position < order.size(); ++position)
                {
                    bool follows = false;
                    order[position - 1]->for_each_successor(
                        [&follows, next = order[position]](Internal::Graph::Node* succ)
                        {
                            follows = follows || succ == next;
                        });
                    if (!follows)
                        return true;
                }
                return false;
            }
        } // namespace

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         */
        Internal::Graph::ChainPlan classify_chain(const Internal::Graph::CompiledGraph& graph, RuntimeContext& context)
        {
            const Internal::Graph::Node* sentinel = graph.sentinel.get();
            const bool has_coordinator = context.offload() != nullptr;
            std::size_t node_count = 0;
            int device = -1;

            for (const auto& owned : graph.all_nodes)
            {
                const Internal::Graph::Node* node = owned.get();
                if (!node || node == sentinel)
                    continue;
                ++node_count;

                if (!node_kind_is_chainable(*node, has_coordinator))
                    return {};

                const int index = chained_queue_index(*node, context);
                if (index < 0 || (device >= 0 && index != device))
                    return {};
                device = index;
            }

            if (node_count == 0)
                return {};

            Internal::Graph::ChainPlan plan;
            plan.order = topological_order(graph);
            if (plan.order.size() != node_count || orders_unordered_host_work(plan.order))
                return {};

            plan.chainable = true;
            plan.queue_index = static_cast<std::size_t>(device);
            return plan;
        }

    } // namespace Execution
} // namespace SushiRuntime
