/**************************************************************************/
/* region_composer.hpp                                                    */
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
 * @file region_composer.hpp
 * @brief Declares RegionComposer, which stitches the plan of a DynamicGraph from its regions.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <set>

#include <SushiRuntime/api/detail/graph_id.hpp>
#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/plan.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        class RegionComposerState;
        class GraphPartition;

        /**
         * @brief Owns the compiled sub-graph of every region and stitches them into one plan.
         *
         * A move leaves the source with no region and no plan, ready to compose again.
         * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
         */
        class RegionComposer
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                /**
                 * @brief Rebuilds and places the changed regions, re-wires them, renumbers nodes.
                 * @param recorder_of Called as recorder_of(key); null when the region is gone.
                 * @return Nothing, or a Fail rule's refusal; a refused commit changes nothing.
                 * @pre A commit that threw is repeated with the dirty and dropped keys it had.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API Result<void>
                commit(const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
                       const std::function<const Recorder*(RegionKey)>& recorder_of,
                       const ContextHandle& context);

                /**
                 * @brief Returns whether the context's placement rules or its offload coordinator
                 *        changed since the last commit; false before the first one.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool
                placement_changed(const ContextHandle& context) const noexcept;

                /**
                 * @brief Re-arms the live in-degree of every node and of the sentinel, in
                 *        O(total nodes).
                 * @pre composed() is true.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                SUSHIRUNTIME_API void rearm() noexcept;

                /** @brief Returns how many times the plan has been composed. */
                [[nodiscard]] SUSHIRUNTIME_API std::size_t compose_count() const noexcept;

                /**
                 * @brief Returns how many times a commit ran the coherence planner over a region.
                 *
                 * A step that replays the composed plan runs no planner and leaves it unchanged.
                 */
                [[nodiscard]] SUSHIRUNTIME_API std::size_t coherence_plan_count() const noexcept;

                /**
                 * @brief Returns whether a region's copy plan is not steady, or the host wrote one
                 *        of its buffers since it was made, or a horizon put another partition of the
                 *        graph in force, so the next run must commit and plan it again.
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool coherence_replan_due(const ContextHandle& context) const noexcept;

                /**
                 * @brief Returns whether a composed plan stands.
                 * @return False before the first commit and after a commit that threw midway.
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool composed() const noexcept;

                /**
                 * @brief Returns whether a region's cached sub-graph matches its recorder.
                 * @return False for a region never built, or built at another generation.
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool is_current(RegionKey key,
                                                               const Recorder& recorder) const;

                /**
                 * @brief Returns the registration index a rule of a region carries in the plan.
                 * @param rule Position in the region's recording order, one per rule.
                 * @return The index, or Detail::K_NO_REGISTRATION before the region is built.
                 */
                [[nodiscard]] SUSHIRUNTIME_API std::uint32_t
                registration_of(RegionKey key, std::size_t rule) const noexcept;

            protected:
                /**
                 * @brief Names the graph for the profiler; the next commit copies the label into the plan's node table.
                 *
                 * Changing it forces no commit.
                 */
                void set_label(std::string label) { label_ = std::move(label); }

                /**
                 * @brief Makes every later commit classify the plan for the in-order chain, or
                 *        stops it; off until called.
                 *
                 * Changing it forces no commit; chain_unclassified() tells the owner one is due.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                void allow_chain(bool allowed) noexcept { chain_allowed_ = allowed; }

                /** @brief Returns whether a commit classifies the plan for the in-order chain. */
                [[nodiscard]] bool chain_allowed() const noexcept { return chain_allowed_; }

                /**
                 * @brief Returns whether the chain is allowed and no commit since has classified
                 *        the plan.
                 */
                [[nodiscard]] SUSHIRUNTIME_API bool chain_unclassified() const noexcept;

                /**
                 * @brief Returns the process-wide id of this graph, never 0 for a graph in use.
                 * @return 0 after a move from this composer.
                 */
                [[nodiscard]] std::uint64_t id() const noexcept { return id_.value(); }

                /** @brief Starts with no region and no plan. */
                SUSHIRUNTIME_API RegionComposer() noexcept;

                /**
                 * @brief Takes over another composer's regions and plan.
                 * @param other Left with no region, no plan and a compose count of zero.
                 */
                SUSHIRUNTIME_API RegionComposer(RegionComposer&& other) noexcept;

                /**
                 * @brief Replaces this composer's regions and plan with another's.
                 * @param other Left with no region, no plan and a compose count of zero.
                 */
                SUSHIRUNTIME_API RegionComposer& operator=(RegionComposer&& other) noexcept;

                RegionComposer(const RegionComposer&) = delete;
                RegionComposer& operator=(const RegionComposer&) = delete;

                /** @brief Frees every region's sub-graph and the plan. */
                SUSHIRUNTIME_API ~RegionComposer();

                /** @brief Returns whether the stitched plan has a node to start from. */
                [[nodiscard]] SUSHIRUNTIME_API bool has_roots() const noexcept;

                /**
                 * @brief Returns the stitched plan; the owning graph and its test seams read it.
                 * @pre commit() has been called since construction or the last move from here.
                 */
                [[nodiscard]] SUSHIRUNTIME_API Plan* compiled_plan() noexcept;

                /**
                 * @brief Returns the stitched plan, read-only.
                 * @pre commit() has been called since construction or the last move from here.
                 */
                [[nodiscard]] SUSHIRUNTIME_API const Plan* compiled_plan() const noexcept;

                /**
                 * @brief Returns the partition report of this graph's plan in force, with what its runs measured.
                 *
                 * Each graph partitions in its own session, so the report holds this graph's plan and runs
                 * only. It is empty before a partition: one device, no profile file, or nothing planned yet.
                 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
                 */
                [[nodiscard]] SUSHIRUNTIME_API PartitionReport partition_report() const;

                /** @brief Returns the partition that places this graph and owns its partition session; null before the first commit. */
                [[nodiscard]] SUSHIRUNTIME_API const GraphPartition* graph_partition() const noexcept;

            private:
                /** @brief The regions, the edges between them and the plan; null until a commit. */
                std::unique_ptr<Detail::RegionComposerState> state_;

                /** @brief The graph's label; a commit copies it into the plan. */
                std::string label_;

                /** @brief The identity of this graph; a commit copies it into the plan. */
                Detail::GraphId id_;

                /** @brief Whether a commit classifies the plan for the in-order chain; a move copies it. */
                bool chain_allowed_ = false;
        };
    } // namespace Detail
} // namespace SushiRuntime
