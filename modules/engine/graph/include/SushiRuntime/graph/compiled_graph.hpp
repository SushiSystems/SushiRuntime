/**************************************************************************/
/* compiled_graph.hpp                                                     */
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
 * @file compiled_graph.hpp
 * @brief Declares CompiledGraph, the plan TaskGraph::compile() produces.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/report/node_table.hpp>
#include <SushiRuntime/report/run_observer.hpp>
// CompiledGraph is exported, so Node must be complete here.
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/chain_plan.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Compiled representation of a task graph ready for execution.
         *
         * TaskGraph::compile() produces this struct. It contains the topologically
         * resolved root nodes and the sentinel node that signals graph completion.
         * The SchedulerExecutionEngine consumes it; TaskGraph only produces it.
         */
        struct SUSHIRUNTIME_API CompiledGraph
        {
            /** @brief Nodes with no pending incoming dependencies (DAG entry points). */
            std::vector<SushiRuntime::Internal::Graph::Node*> roots;

            /** @brief Sentinel node injected at the end of the graph. */
            sushi_ptr<SushiRuntime::Internal::Graph::Node> sentinel;

            /**
             * @brief Owns every node; the dependencies between nodes are raw pointers into it.
             */
            std::vector<sushi_ptr<SushiRuntime::Internal::Graph::Node>> all_nodes;

            /**
             * @brief Snapshots each node's compiled in-degree so the plan can replay.
             * @pre compile() has run.
             * @see modules/engine/execution/README.md
             */
            void capture_base_state();

            /**
             * @brief Sizes profiling_multiplicities and builds node_table from all_nodes.
             * @pre all_nodes carries its final registration indices for this compose.
             * @see modules/engine/execution/README.md
             */
            void capture_profiling_state();

            /**
             * @brief Restores every node's per-run state to its compiled baseline.
             *
             * Between two executions the caller may write Shared or Host USM that no node
             * of the plan writes; the next execution reads those bytes.
             * @pre capture_base_state() has been called.
             * @see modules/engine/graph/README.md
             */
            void rearm() noexcept;

            /** @brief Per-node compiled in-degree baseline, filled by capture_base_state(). */
            std::vector<std::int32_t> base_in_degree;
            /** @brief Sentinel's compiled in-degree baseline. */
            std::int32_t sentinel_in_degree = 0;

            /** @brief Whether this plan dispatches its device nodes on the profiling queue, as the runtime's observer asked when it was built. */
            bool profiling = false;

            /** @brief Per-run node event records and device clock pairings, live only while profiling is on. */
            Internal::Observability::ProfilingLedger profiling_ledger;

            /**
             * @brief Per-registration-index count of internal nodes sharing it.
             *
             * Computed once in capture_base_state(); sized to max(registration_index)+1
             * and indexed by registration_index. Sizes profiling_ledger's rows.
             */
            std::vector<std::uint32_t> profiling_multiplicities;

            /**
             * @brief Names and operations of the plan's nodes by registration index.
             *
             * Built once in capture_profiling_state() and shared with every observation of the
             * plan's runs.
             */
            std::shared_ptr<const NodeTable> node_table;

            /** @brief Label of the graph at the time of the build; the table copies it in capture_profiling_state(). */
            std::string label;

            /** @brief Id of the graph that owns the plan, set before capture_base_state(); 0 when none. */
            std::uint64_t graph_id = 0;

            /**
             * @brief The observation every run of the plan fills and hands to the observers.
             *
             * Cleared and refilled each run, so an observed run allocates nothing once its vectors have grown.
             */
            RunObservation observation;

            /** @brief Whether and how this plan runs as an in-order chain; set by the fluent Graph at build. */
            ChainPlan chain;
        };

    } // namespace Graph
} // namespace SushiRuntime
