/****************************************************************/
/* chain_plan.cpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file chain_plan.cpp
 * @brief Classifies a compiled plan for the in-order chain and orders its nodes.
 * @author Mustafa Garip
 */

#include <cstdint>
#include <deque>
#include <vector>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        namespace
        {
            /**
             * @brief Reports whether one node's kind and bindings allow it on the chain.
             * @param node A compiled, non-sentinel node.
             * @return False for native host work or an offload candidate.
             */
            bool node_kind_is_chainable(const Graph::Node& node) noexcept
            {
                using Mode = Scheduler::Task::ExecutionMode;
                const bool submits_device = node.get_execution_mode() == Mode::KERNEL ||
                                            node.submits_device_work();
                if (!submits_device)
                    return false;
                if (node.metadata.op_id.value != 0)
                    return false;
                return true;
            }

            /**
             * @brief Resolves the one device queue every region of a node belongs to.
             * @param node A compiled, non-sentinel node.
             * @param ctx  The context whose registry owns the regions.
             * @return The queue index, or -1 for no region, an unknown region, or two devices.
             */
            int owning_queue_index(const Graph::Node& node, RuntimeContext& ctx) noexcept
            {
                int owner = -1;
                auto visit = [&](const std::vector<Core::ResourceRegion>& regions)
                {
                    for (const auto& r : regions)
                    {
                        if (!r.base)
                            continue;
                        const int index = ctx.context_index_for_ptr(r.base);
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
            std::vector<Graph::Node*> topological_order(const CompiledGraph& graph)
            {
                const Graph::Node* sentinel = graph.sentinel.get();
                const std::size_t count = graph.all_nodes.size();

                std::vector<std::int32_t> remaining(graph.base_in_degree.begin(),
                                                    graph.base_in_degree.end());
                remaining.resize(count, 0);

                std::deque<Graph::Node*> ready;
                for (std::size_t i = 0; i < count; ++i)
                {
                    Graph::Node* node = graph.all_nodes[i].get();
                    if (node && node != sentinel && remaining[i] == 0)
                        ready.push_back(node);
                }

                std::vector<Graph::Node*> order;
                order.reserve(count);
                while (!ready.empty())
                {
                    Graph::Node* node = ready.front();
                    ready.pop_front();
                    order.push_back(node);
                    node->for_each_successor([&](Graph::Node* succ)
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
        ChainPlan classify_chain(const CompiledGraph& graph, RuntimeContext& ctx)
        {
            const Graph::Node* sentinel = graph.sentinel.get();
            std::size_t node_count = 0;
            int device = -1;

            for (const auto& owned : graph.all_nodes)
            {
                const Graph::Node* node = owned.get();
                if (!node || node == sentinel)
                    continue;
                ++node_count;

                if (!node_kind_is_chainable(*node))
                    return {};

                const int index = owning_queue_index(*node, ctx);
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
