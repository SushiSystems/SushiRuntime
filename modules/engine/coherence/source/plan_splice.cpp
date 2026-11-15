/**************************************************************************/
/* plan_splice.cpp                                                        */
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
 * @file plan_splice.cpp
 * @brief Implements splice_plan.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/plan_splice.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            using Node = Graph::Node;

            /** @brief Names one instance: a buffer on a space. */
            using InstanceKey = std::pair<GlobalHandle, std::uint32_t>;

            /** @brief Adds the edges of a plan to a graph, each once. */
            class EdgeWriter final
            {
                public:
                    /** @brief Adds from -> to and raises the in-degree of to, unless the edge exists. */
                    void link(Node* from, Node* to)
                    {
                        if (from == to || !linked_.insert({from, to}).second)
                            return;
                        from->add_successor(to);
                        to->in_degree.fetch_add(1, std::memory_order_relaxed);
                    }

                    /** @brief Links every node of sources to to. */
                    void link_all(const std::vector<Node*>& sources, Node* to)
                    {
                        for (Node* source : sources)
                            link(source, to);
                    }

                private:
                    /// @brief Edges added so far; only asked whether an edge exists.
                    std::set<std::pair<Node*, Node*>> linked_;
            };

            /** @brief Returns whether node has no successor. */
            bool is_leaf(const Node& node) noexcept
            {
                return node.inline_successor_count.load(std::memory_order_relaxed) == 0 &&
                       node.successor_list.load(std::memory_order_relaxed) == nullptr;
            }

            /** @brief Returns the instances a planned step makes, writes, reads or frees. */
            std::vector<InstanceKey> instances_of(const PlannedStep& step)
            {
                std::vector<InstanceKey> keys{{step.handle, step.space.value()}};
                if (step.kind == PlannedStepKind::Copy)
                    keys.push_back({step.handle, step.source.value()});
                return keys;
            }
        } // namespace

        Result<CopyPlan> splice_plan(std::vector<sushi_ptr<Node>>& nodes, const SpaceOfNode& space_of,
                                 CoherencePlanner& planner, PlannedNodeFactory& factory)
        {
            std::vector<NodeAccess> accesses(nodes.size());
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const std::optional<MemorySpaceID> space = space_of(*nodes[index]);
                if (!space)
                    continue;
                accesses[index] = NodeAccess{true, *space, nodes[index]->io_reads_, nodes[index]->io_writes_};
            }

            Result<CopyPlan> planned = planner.plan(accesses);
            if (!planned)
                return make_error(std::move(planned.error()));
            const std::vector<PlannedStep>& steps = planned.value().steps;
            if (steps.empty())
                return std::move(planned.value());

            // Every node is built before the graph changes, so a refused step leaves it whole.
            std::vector<sushi_ptr<Node>> made;
            made.reserve(steps.size());
            for (const PlannedStep& step : steps)
            {
                Result<sushi_ptr<Node>> node = factory.make(step);
                if (!node)
                    return make_error(std::move(node.error()));
                made.push_back(std::move(node.value()));
            }

            std::unordered_map<const Node*, std::vector<Node*>> predecessors;
            for (const sushi_ptr<Node>& node : nodes)
                node->for_each_successor([&predecessors, &node](Node* successor)
                {
                    predecessors[successor].push_back(node.get());
                });

            EdgeWriter edges;
            std::map<InstanceKey, std::vector<Node*>> touching;
            std::map<InstanceKey, std::vector<Node*>> users;
            std::map<std::uint32_t, std::vector<Node*>> evicted;

            // An allocation waits for the evictions that made room on its space.
            const auto order_by_capacity = [&edges, &evicted](const PlannedStep& step, Node* step_node)
            {
                if (step.kind == PlannedStepKind::Allocate)
                    edges.link_all(evicted[step.space.value()], step_node);
                else if (step.kind == PlannedStepKind::Free && step.evicts)
                    evicted[step.space.value()].push_back(step_node);
            };
            std::size_t next = 0;
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                Node* const target = nodes[index].get();
                for (; next < steps.size() && steps[next].node == index; ++next)
                {
                    Node* const step_node = made[next].get();
                    edges.link_all(predecessors[target], step_node);
                    for (const InstanceKey& key : instances_of(steps[next]))
                    {
                        edges.link_all(touching[key], step_node);
                        // An eviction may reach instances no edge ties to the node it precedes.
                        if (steps[next].evicts)
                            edges.link_all(users[key], step_node);
                    }
                    order_by_capacity(steps[next], step_node);
                    edges.link(step_node, target);
                    for (const InstanceKey& key : instances_of(steps[next]))
                        touching[key].push_back(step_node);
                }

                if (!accesses[index].planned)
                    continue;
                const std::uint32_t space = accesses[index].space.value();
                for (const BufferRegion& region : accesses[index].reads)
                {
                    edges.link_all(touching[{region.base, space}], target);
                    users[{region.base, space}].push_back(target);
                }
                for (const BufferRegion& region : accesses[index].writes)
                {
                    edges.link_all(touching[{region.base, space}], target);
                    users[{region.base, space}].push_back(target);
                }
            }

            std::vector<Node*> leaves;
            for (const sushi_ptr<Node>& node : nodes)
                if (is_leaf(*node))
                    leaves.push_back(node.get());
            for (std::size_t step = 0; step < next; ++step)
                if (is_leaf(*made[step]))
                    leaves.push_back(made[step].get());

            for (; next < steps.size(); ++next)
            {
                Node* const step_node = made[next].get();
                edges.link_all(leaves, step_node);
                order_by_capacity(steps[next], step_node);
                for (const InstanceKey& key : instances_of(steps[next]))
                {
                    edges.link_all(touching[key], step_node);
                    touching[key].push_back(step_node);
                }
            }

            nodes.insert(nodes.end(), made.begin(), made.end());
            return std::move(planned.value());
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
