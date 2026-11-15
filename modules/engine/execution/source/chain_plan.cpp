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
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
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
             * @param memory  The memory whose registry owns the regions.
             * @return The queue index, or -1 for no region, an unknown region, two devices, or a
             *         placement placement_of() refuses, which the scheduler then reports.
             */
            int chained_queue_index(const Internal::Graph::Node& node, const Internal::Graph::PlacementMemory& memory)
            {
                int owner = -1;
                auto visit = [&](const std::vector<BufferRegion>& regions)
                {
                    for (const auto& r : regions)
                    {
                        if (!r.valid())
                            continue;
                        const std::optional<std::size_t> device = memory.recorded_device(r.base);
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
                if (!placed || placed.value() >= memory.device_count())
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

            /** @brief Marks a node that is not a HOST node in the rank table of for_each_unordered_host_pair(). */
            constexpr std::uint32_t K_NOT_HOST = ~std::uint32_t{0};

            /**
             * @brief Calls @p visit for every pair of HOST nodes no path of the plan orders.
             *
             * One walk of @p order carries, per node, the set of HOST nodes that reach it; a HOST
             * node meets each earlier HOST node missing from its set. O((nodes + edges) * hosts / 64).
             * @param visit Called as visit(earlier, later); a false return stops the walk.
             * @return False if @p visit stopped the walk.
             */
            template <class Visit>
            bool for_each_unordered_host_pair(const Internal::Graph::CompiledGraph& graph,
                                              const std::vector<Internal::Graph::Node*>& order, Visit&& visit)
            {
                using Mode = Internal::Scheduler::Task::ExecutionMode;
                const Internal::Graph::Node* sentinel = graph.sentinel.get();
                const std::size_t count = graph.all_nodes.size();

                std::vector<std::uint32_t> rank(count, K_NOT_HOST);
                std::vector<const Internal::Graph::Node*> hosts;
                for (const Internal::Graph::Node* node : order)
                {
                    if (node->execution_mode() != Mode::Host)
                        continue;
                    rank[node->plan_index] = static_cast<std::uint32_t>(hosts.size());
                    hosts.push_back(node);
                }
                if (hosts.size() < 2)
                    return true;

                const std::size_t words = (hosts.size() + 63) / 64;
                std::vector<std::uint64_t> reached_by(count * words, 0);
                for (Internal::Graph::Node* node : order)
                {
                    std::uint64_t* mine = &reached_by[node->plan_index * words];
                    const std::uint32_t own = rank[node->plan_index];
                    if (own != K_NOT_HOST)
                    {
                        for (std::uint32_t earlier = 0; earlier < own; ++earlier)
                        {
                            const bool reaches = ((mine[earlier / 64] >> (earlier % 64)) & 1u) != 0;
                            if (!reaches && !visit(*hosts[earlier], *node))
                                return false;
                        }
                        mine[own / 64] |= std::uint64_t{1} << (own % 64);
                    }

                    node->for_each_successor([&](Internal::Graph::Node* succ)
                    {
                        const std::size_t index = succ->plan_index;
                        if (succ == sentinel || index >= count || graph.all_nodes[index].get() != succ)
                            return;
                        std::uint64_t* theirs = &reached_by[index * words];
                        for (std::size_t w = 0; w < words; ++w)
                            theirs[w] |= mine[w];
                    });
                }
                return true;
            }

            /**
             * @brief Reports whether every region a node names is tracked and fixed across steps.
             * @return False for a region with no base, or for a node whose live lists re-point
             *         its addresses before each step.
             */
            bool declares_every_byte(const Internal::Graph::Node& node) noexcept
            {
                const Internal::Graph::OffloadState* offload = node.offload_state();
                if (offload != nullptr && (offload->live_reads != nullptr || offload->live_writes != nullptr))
                    return false;
                const auto tracked = [](const BufferRegion& region) { return region.valid(); };
                return std::all_of(node.io_reads_.begin(), node.io_reads_.end(), tracked) &&
                       std::all_of(node.io_writes_.begin(), node.io_writes_.end(), tracked);
            }

            /**
             * @brief Reports whether two nodes declare every byte they touch and no byte in common,
             *        reads included.
             */
            bool share_no_declared_byte(const Internal::Graph::Node& a, const Internal::Graph::Node& b) noexcept
            {
                if (!declares_every_byte(a) || !declares_every_byte(b))
                    return false;

                const auto meets_b = [&b](const BufferRegion& region)
                {
                    const auto meets = [&region](const BufferRegion& other) { return region.overlaps(other); };
                    return std::any_of(b.io_reads_.begin(), b.io_reads_.end(), meets) ||
                           std::any_of(b.io_writes_.begin(), b.io_writes_.end(), meets);
                };
                return std::none_of(a.io_reads_.begin(), a.io_reads_.end(), meets_b) &&
                       std::none_of(a.io_writes_.begin(), a.io_writes_.end(), meets_b);
            }

            /**
             * @brief Reports whether the chain may run in turn every pair of HOST nodes the plan
             *        leaves unordered.
             * @param graph The plan @p order was built from, after capture_base_state().
             * @param order A topological order of every non-sentinel node of the plan.
             * @see docs/design/GRAPH_REPLAY/README.md
             */
            bool admits_unordered_host_work(const Internal::Graph::CompiledGraph& graph,
                                            const std::vector<Internal::Graph::Node*>& order,
                                            UnorderedHostWork rule)
            {
                return for_each_unordered_host_pair(graph, order,
                    [rule](const Internal::Graph::Node& earlier, const Internal::Graph::Node& later)
                    {
                        return rule == UnorderedHostWork::InTurnWhenDisjoint &&
                               share_no_declared_byte(earlier, later);
                    });
            }
        } // namespace

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         */
        Internal::Graph::ChainPlan classify_chain(const Internal::Graph::CompiledGraph& graph, RuntimeContext& context,
                                                  UnorderedHostWork unordered_host)
        {
            return classify_chain(graph, Internal::Graph::USMPlacementMemory(context.memory(), context.handles()),
                                  context.offload() != nullptr, unordered_host);
        }

        /**
         * @brief Classifies a compiled plan for the in-order chain, reading memory through a port.
         */
        Internal::Graph::ChainPlan classify_chain(const Internal::Graph::CompiledGraph& graph,
                                                  const Internal::Graph::PlacementMemory& memory,
                                                  bool has_coordinator, UnorderedHostWork unordered_host)
        {
            const Internal::Graph::Node* sentinel = graph.sentinel.get();
            std::size_t node_count = 0;
            int device = -1;

            // The first reason is kept and logged once, where the plan is classified.
            const auto refuse = [](const char* reason)
            {
                SR_LOG_DEBUG(K_EXECUTION_LOG_CATEGORY, "The plan runs on the scheduler, not as a chain: {}.", reason);
                Internal::Graph::ChainPlan plan;
                plan.not_chainable = reason;
                return plan;
            };

            for (const auto& owned : graph.all_nodes)
            {
                const Internal::Graph::Node* node = owned.get();
                if (!node || node == sentinel)
                    continue;
                ++node_count;

                if (!node_kind_is_chainable(*node, has_coordinator))
                    return refuse(node->execution_mode() == Internal::Scheduler::Task::ExecutionMode::Kernel ||
                                          node->submits_device_work()
                                      ? "an installed offload coordinator acts on a node"
                                      : "a node is native host work");

                const int index = chained_queue_index(*node, memory);
                if (index < 0)
                    return refuse("a node has no USM region on one known device, or its placement was refused");
                if (device >= 0 && index != device)
                    return refuse("the nodes' regions live on two devices");
                device = index;
            }

            if (node_count == 0)
                return refuse("the plan holds no node");

            Internal::Graph::ChainPlan plan;
            plan.order = topological_order(graph);
            if (plan.order.size() != node_count)
                return refuse("the plan has no complete topological order");
            if (!admits_unordered_host_work(graph, plan.order, unordered_host))
                return refuse(unordered_host == UnorderedHostWork::Refused
                                  ? "a chain would order host work the dependencies leave unordered"
                                  : "a chain would order host work the dependencies leave unordered over "
                                    "shared or undeclared data");

            plan.chainable = true;
            plan.queue_index = static_cast<std::size_t>(device);
            return plan;
        }

    } // namespace Execution
} // namespace SushiRuntime
