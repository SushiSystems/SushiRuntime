/**************************************************************************/
/* graph_partition.hpp                                                    */
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
 * @file graph_partition.hpp
 * @brief Declares GraphPartition, which places every node of one graph from one partition of the whole graph.
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/partition_session/graph_partition_problem.hpp>
#include <SushiRuntime/partition_session/partition_session.hpp>
#include <SushiRuntime/partitioning/partition_assignment.hpp>
#include <SushiRuntime/report/partition_report.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Partitions the nodes of every region of one graph as one problem and states the answer on them.
         *
         * A node this object placed counts as free in the next problem. The graph's first problem opens
         * the PartitionSession this object owns, so no other graph's plan or runs reach it. The partitioner
         * runs again only when the problem's hash changed or a horizon put another plan in force.
         * @see docs/design/GLOBAL_HANDLE/PARTITIONING.md
         */
        class GraphPartition final
        {
            public:
                /**
                 * @brief Returns whether a horizon put another plan in force for the problem this object
                 *        placed last, so the graph must be placed again before its next run.
                 */
                [[nodiscard]] bool rebalanced() const noexcept;

                /**
                 * @brief Returns the nodes this object placed that the partition of nodes now puts
                 *        elsewhere or cuts anew; nothing is placed.
                 * @param nodes Every work node of the graph, in region then recording order.
                 * @return The moved nodes, or the refusal, with each pool's shortfall, of a job no plan fits.
                 */
                [[nodiscard]] Result<std::set<const Internal::Graph::Node*>>
                moved(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                      Internal::Execution::RuntimeContext& runtime);

                /**
                 * @brief States on every movable node of nodes the device the partition of nodes gives it.
                 *
                 * The graph's session holds the problem and its answer for report().
                 * @param nodes Every work node of the graph, in region then recording order.
                 * @return The refusal of a job no plan fits, or the error of a fixed node's placement.
                 */
                [[nodiscard]] Result<void> place(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                                                 Internal::Execution::RuntimeContext& runtime);

                /** @brief Returns the graph's own session; null until its first problem opened one. */
                [[nodiscard]] Internal::Coherence::PartitionSession* session() const noexcept
                {
                    return session_.get();
                }

                /** @brief Returns problem_hash() of the problem the graph was placed by last; zero before the first. */
                [[nodiscard]] std::uint64_t placed_problem_hash() const noexcept
                {
                    return problem_hash_ ? *problem_hash_ : 0;
                }

                /** @brief Returns the report of the graph's plan in force with what its runs measured; empty before one. */
                [[nodiscard]] PartitionReport report() const;

            private:
                /**
                 * @brief Builds the problem of nodes and answers it, partitioning only when its hash changed.
                 * @return Nothing when no partition applies; the error of a refusal or a fixed node.
                 */
                [[nodiscard]] Result<std::optional<Internal::Coherence::GraphPartitionProblem>>
                answer(const std::vector<sushi_ptr<Internal::Graph::Node>>& nodes,
                       Internal::Execution::RuntimeContext& runtime);

                /// @brief The nodes the last place() stated a device on; held so no other node takes an address.
                std::vector<sushi_ptr<Internal::Graph::Node>> placed_;

                /// @brief The addresses of placed_, for the problem builder.
                std::set<const Internal::Graph::Node*> placed_addresses_;

                /// @brief Hash of the last problem answered; nothing before the first.
                std::optional<std::uint64_t> problem_hash_;

                /// @brief The graph's own session; null until its first problem opened one.
                std::shared_ptr<Internal::Coherence::PartitionSession> session_;

                /// @brief The session's generation when this object took its answer last.
                std::uint64_t generation_ = 0;

                /// @brief Whether the last answer() ran the partitioner rather than reusing its answer.
                bool answered_again_ = false;

                /// @brief The answer to the last problem.
                Internal::Coherence::PartitionAssignment assignment_;
        };
    } // namespace Detail
} // namespace SushiRuntime
