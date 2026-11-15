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
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SushiRuntime/task/placed_split.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            using Node = Graph::Node;

            /** @brief Names one instance: a buffer on a space. */
            using InstanceKey = std::pair<GlobalHandle, std::uint32_t>;

            /** @brief Holds the planned nodes that ordered one instance so far, by what they did to it. */
            struct InstanceOrder
            {
                std::vector<Node*> writers; /**< Steps and effects that made, wrote or freed the instance. */
                std::vector<Node*> readers; /**< Copies that read the instance as their source. */
                std::vector<Node*> records; /**< Nodes that record a copy into the instance on the map. */
            };

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

            /** @brief Links every node that ordered an instance to to. */
            void link_order(EdgeWriter& edges, const InstanceOrder& order, Node* to)
            {
                edges.link_all(order.writers, to);
                edges.link_all(order.readers, to);
                edges.link_all(order.records, to);
            }

            /** @brief Returns the parts of split as the planner reads them, each on its device's space. */
            std::vector<NodeAccessPart> parts_of(const Graph::PlacedSplit& split, const SpaceOfDevice& space_of_device)
            {
                std::vector<NodeAccessPart> parts;
                parts.reserve(split.parts.size());
                for (std::size_t part = 0; part < split.parts.size() && part < split.access.size(); ++part)
                    parts.push_back(NodeAccessPart{space_of_device(split.parts[part].device), split.access[part].reads,
                                                   split.access[part].writes});
                return parts;
            }
        } // namespace

        Result<CopyPlan> splice_plan(std::vector<sushi_ptr<Node>>& nodes, const SpaceOfNode& space_of,
                                 CoherencePlanner& planner, PlannedNodeFactory& factory,
                                 const SpaceOfDevice& space_of_device)
        {
            std::vector<NodeAccess> accesses(nodes.size());
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const std::optional<MemorySpaceID> space = space_of(*nodes[index]);
                if (!space)
                    continue;
                accesses[index] = NodeAccess{true, *space, nodes[index]->io_reads_, nodes[index]->io_writes_, {}};
                const std::shared_ptr<const Graph::PlacedSplit>& split = nodes[index]->placed_split;
                if (split && space_of_device)
                    accesses[index].parts = parts_of(*split, space_of_device);
            }

            Result<CopyPlan> planned = planner.plan(accesses);
            if (!planned)
                return make_error(std::move(planned.error()));
            const std::vector<PlannedStep>& steps = planned.value().steps;
            const std::vector<PlannedEffect>& effects = planned.value().effects;
            if (steps.empty() && effects.empty())
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
            std::vector<sushi_ptr<Node>> made_effects;
            made_effects.reserve(effects.size());
            for (const PlannedEffect& effect : effects)
            {
                Result<sushi_ptr<Node>> node = factory.make_effect(effect);
                if (!node)
                    return make_error(std::move(node.error()));
                made_effects.push_back(std::move(node.value()));
            }
            // A copy's record node; null for every other step.
            std::vector<sushi_ptr<Node>> made_records(steps.size());
            for (std::size_t step = 0; step < steps.size(); ++step)
            {
                if (steps[step].kind != PlannedStepKind::Copy)
                    continue;
                Result<sushi_ptr<Node>> node = factory.make_copy_record(steps[step]);
                if (!node)
                    return make_error(std::move(node.error()));
                made_records[step] = std::move(node.value());
            }

            std::unordered_map<const Node*, std::vector<Node*>> predecessors;
            for (const sushi_ptr<Node>& node : nodes)
                node->for_each_successor([&predecessors, &node](Node* successor)
                {
                    predecessors[successor].push_back(node.get());
                });

            EdgeWriter edges;
            std::map<InstanceKey, InstanceOrder> touching;
            std::map<InstanceKey, std::vector<Node*>> users;
            std::map<std::uint32_t, std::vector<Node*>> evicted;
            // The last Hold or Write of each buffer; every later step, effect and use of it waits for it.
            std::map<GlobalHandle, Node*> barrier;

            // Links every node that touched an instance of handle, and its barrier, to to.
            const auto after_handle = [&edges, &touching, &barrier](GlobalHandle handle, Node* to)
            {
                for (auto key = touching.lower_bound({handle, 0}); key != touching.end() && key->first.first == handle;
                     ++key)
                    link_order(edges, key->second, to);
                const auto held = barrier.find(handle);
                if (held != barrier.end())
                    edges.link(held->second, to);
            };
            const auto link_barrier = [&edges, &barrier](GlobalHandle handle, Node* to)
            {
                const auto held = barrier.find(handle);
                if (held != barrier.end())
                    edges.link(held->second, to);
            };
            std::size_t next_effect = 0;
            for (; next_effect < effects.size() && effects[next_effect].kind == PlannedEffectKind::Hold; ++next_effect)
                barrier[effects[next_effect].handle] = made_effects[next_effect].get();

            // An allocation waits for the evictions that made room on its space.
            const auto order_by_capacity = [&edges, &evicted](const PlannedStep& step, Node* step_node)
            {
                if (step.kind == PlannedStepKind::Allocate)
                    edges.link_all(evicted[step.space.value()], step_node);
                else if (step.kind == PlannedStepKind::Free && step.evicts)
                    evicted[step.space.value()].push_back(step_node);
            };
            // A copy follows only the writers of its source, so two copies of one source run together.
            const auto place_step = [&](std::size_t index, Node* step_node)
            {
                const PlannedStep& step = steps[index];
                link_barrier(step.handle, step_node);
                order_by_capacity(step, step_node);
                const InstanceKey written{step.handle, step.space.value()};
                link_order(edges, touching[written], step_node);
                // An eviction may reach instances no edge ties to the node it precedes.
                if (step.evicts)
                    for (const InstanceKey& key : instances_of(step))
                        edges.link_all(users[key], step_node);
                if (step.kind != PlannedStepKind::Copy)
                {
                    touching[written].writers.push_back(step_node);
                    return;
                }
                const InstanceKey read{step.handle, step.source.value()};
                edges.link_all(touching[read].writers, step_node);
                // No node may still read the bytes the copy overwrites.
                edges.link_all(users[written], step_node);
                Node* const record = made_records[index].get();
                edges.link(step_node, record);
                touching[read].readers.push_back(step_node);
                touching[written].writers.push_back(step_node);
                touching[written].records.push_back(record);
            };
            std::size_t next = 0;
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                Node* const target = nodes[index].get();
                for (; next < steps.size() && steps[next].node == index; ++next)
                {
                    Node* const step_node = made[next].get();
                    if (steps[next].kind != PlannedStepKind::Copy)
                        edges.link_all(predecessors[target], step_node);
                    place_step(next, step_node);
                    edges.link(step_node, target);
                }

                if (!accesses[index].planned)
                    continue;
                // A read waits for the instance's writers; a write also for the copies that read it.
                const auto use = [&edges, &touching, &users, &link_barrier, target](
                                     std::uint32_t space, const std::vector<BufferRegion>& regions, bool writes)
                {
                    for (const BufferRegion& region : regions)
                    {
                        link_barrier(region.base, target);
                        const InstanceOrder& order = touching[{region.base, space}];
                        edges.link_all(order.writers, target);
                        if (writes)
                            edges.link_all(order.readers, target);
                        users[{region.base, space}].push_back(target);
                    }
                };
                if (accesses[index].parts.empty())
                {
                    use(accesses[index].space.value(), accesses[index].reads, false);
                    use(accesses[index].space.value(), accesses[index].writes, true);
                }
                for (const NodeAccessPart& part : accesses[index].parts)
                {
                    use(part.space.value(), part.reads, false);
                    use(part.space.value(), part.writes, true);
                }

                // A node's writes are recorded once it has completed; every later step and use of the buffer waits for that.
                for (; next_effect < effects.size() && effects[next_effect].node == index; ++next_effect)
                {
                    const PlannedEffect& effect = effects[next_effect];
                    Node* const effect_node = made_effects[next_effect].get();
                    after_handle(effect.handle, effect_node);
                    edges.link(target, effect_node);
                    barrier[effect.handle] = effect_node;
                    touching[{effect.handle, effect.space.value()}].writers.push_back(effect_node);
                }
            }

            std::vector<Node*> leaves;
            for (const sushi_ptr<Node>& node : nodes)
                if (is_leaf(*node))
                    leaves.push_back(node.get());
            for (std::size_t step = 0; step < next; ++step)
            {
                if (is_leaf(*made[step]))
                    leaves.push_back(made[step].get());
                if (made_records[step] && is_leaf(*made_records[step]))
                    leaves.push_back(made_records[step].get());
            }
            for (std::size_t effect = 0; effect < next_effect; ++effect)
                if (is_leaf(*made_effects[effect]))
                    leaves.push_back(made_effects[effect].get());

            for (; next < steps.size(); ++next)
            {
                Node* const step_node = made[next].get();
                edges.link_all(leaves, step_node);
                place_step(next, step_node);
            }

            // A Leave runs after every node and every step, so the run holds the buffer to its end.
            for (; next_effect < effects.size(); ++next_effect)
            {
                Node* const effect_node = made_effects[next_effect].get();
                edges.link_all(leaves, effect_node);
                after_handle(effects[next_effect].handle, effect_node);
            }

            nodes.insert(nodes.end(), made.begin(), made.end());
            nodes.insert(nodes.end(), made_effects.begin(), made_effects.end());
            for (sushi_ptr<Node>& record : made_records)
                if (record)
                    nodes.push_back(std::move(record));
            return std::move(planned.value());
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
