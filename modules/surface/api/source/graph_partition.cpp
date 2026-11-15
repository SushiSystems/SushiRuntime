/**************************************************************************/
/* graph_partition.cpp                                                    */
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
 * @file graph_partition.cpp
 * @brief Implements GraphPartition.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include "graph_partition.hpp"

#include <cstddef>
#include <utility>

#include <SushiRuntime/partition_session/partition_apply.hpp>
#include <SushiRuntime/partition_session/partition_sessions.hpp>
#include <SushiRuntime/partitioning/problem_hash.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        Result<std::optional<Internal::Coherence::GraphPartitionProblem>>
        GraphPartition::answer(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                               Internal::Execution::RuntimeContext& runtime)
        {
            using Problem = std::optional<Internal::Coherence::GraphPartitionProblem>;
            Internal::Coherence::PartitionSessions& sessions = runtime.partitioning();
            answered_again_ = false;
            if (runtime.devices().count() < 2 || (session_ == nullptr && !sessions.is_active()))
                return Problem();
            const Internal::Graph::USMPlacementMemory memory(runtime.memory(), runtime.handle_ports().residency_reader);
            Result<Internal::Coherence::GraphPartitionProblem> graph = Internal::Coherence::make_graph_partition_problem(
                nodes, memory, runtime.handle_ports().residency_reader, sessions.devices(), placed_addresses_);
            if (!graph)
                return make_error(std::move(graph.error()));
            if (graph.value().planned_buffers == 0)
                return Problem();

            const std::uint64_t hash = Internal::Coherence::problem_hash(graph.value().problem);
            // The graph's first problem opens the session this graph alone partitions, measures and journals in.
            if (session_ == nullptr)
            {
                session_ = sessions.open(hash);
                if (session_ == nullptr)
                    return Problem();
            }
            // The same problem keeps its answer unless a horizon put another plan for it in force.
            const bool same_problem = problem_hash_ && *problem_hash_ == hash;
            const bool rebalanced = same_problem && session_->has_plan() && session_->problem_hash() == hash &&
                                    session_->generation() != generation_;
            answered_again_ = !same_problem || rebalanced;
            if (!answered_again_)
                return Problem(std::move(graph.value()));
            const Result<Internal::Coherence::PartitionAssignment, Internal::Coherence::PartitionRefusal>& answered =
                rebalanced ? session_->answer() : session_->partition(graph.value().problem);
            sessions.partitioned(session_);
            if (!answered)
                return make_error(answered.error().to_error());
            problem_hash_ = hash;
            generation_ = session_->generation();
            assignment_ = answered.value();
            return Problem(std::move(graph.value()));
        }

        bool GraphPartition::rebalanced() const noexcept
        {
            return problem_hash_ && session_ != nullptr && session_->has_plan() &&
                   session_->problem_hash() == *problem_hash_ && session_->generation() != generation_;
        }

        PartitionReport GraphPartition::report() const
        {
            return session_ != nullptr ? session_->report() : PartitionReport{};
        }

        Result<std::set<const Internal::Graph::Node*>>
        GraphPartition::moved(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                              Internal::Execution::RuntimeContext& runtime)
        {
            std::set<const Internal::Graph::Node*> moved;
            Result<std::optional<Internal::Coherence::GraphPartitionProblem>> graph = answer(nodes, runtime);
            if (!graph)
                return make_error(std::move(graph.error()));
            if (!graph.value())
                return moved;
            const Internal::Coherence::GraphPartitionProblem& problem = *graph.value();
            const std::vector<Internal::Coherence::PartitionDevice>& devices = problem.problem.devices;
            for (std::size_t row = 0; row < problem.nodes.size() && row < assignment_.node_devices.size(); ++row)
            {
                const Internal::Graph::Node* node = problem.nodes[row];
                if (problem.problem.nodes[row].placed || placed_addresses_.count(node) == 0)
                    continue;
                const std::optional<std::size_t> stated = node->stated_placement();
                if (!stated || *stated >= devices.size() || !(devices[*stated].key == assignment_.node_devices[row]))
                    moved.insert(node);
            }
            if (!answered_again_)
                return moved;
            // A new answer may cut a node another way, so every cut node, before or after, is placed again.
            for (std::size_t row = 0; row < problem.nodes.size(); ++row)
                if (placed_addresses_.count(problem.nodes[row]) != 0 && problem.nodes[row]->placed_split != nullptr)
                    moved.insert(problem.nodes[row]);
            for (const Internal::Coherence::PlannedSplit& split : assignment_.splits)
                if (split.node < problem.nodes.size() && placed_addresses_.count(problem.nodes[split.node]) != 0)
                    moved.insert(problem.nodes[split.node]);
            return moved;
        }

        Result<void> GraphPartition::place(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                           Internal::Execution::RuntimeContext& runtime)
        {
            Result<std::optional<Internal::Coherence::GraphPartitionProblem>> graph = answer(nodes, runtime);
            if (!graph)
                return make_error(std::move(graph.error()));
            if (!graph.value())
                return {};
            Result<void> applied = Internal::Coherence::apply_partition(*graph.value(), assignment_);
            if (!applied)
                return applied;

            std::vector<sushi_ptr<Internal::Graph::Node>> placed;
            std::set<const Internal::Graph::Node*> addresses;
            for (std::size_t row = 0; row < graph.value()->nodes.size(); ++row)
            {
                if (graph.value()->problem.nodes[row].placed)
                    continue;
                placed.push_back(nodes[row]);
                addresses.insert(nodes[row].get());
            }
            placed_ = std::move(placed);
            placed_addresses_ = std::move(addresses);
            return {};
        }
    } // namespace Detail
} // namespace SushiRuntime
