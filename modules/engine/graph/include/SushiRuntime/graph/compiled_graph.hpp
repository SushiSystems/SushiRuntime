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
#include <vector>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
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
         * The ExecutionEngine consumes it; TaskGraph only produces it.
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
             * @brief Sizes profiling_multiplicities and profiling_host_native from all_nodes.
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

            /** @brief Whether this plan dispatches its device nodes on the profiling queue. */
            bool profiling = false;

            /** @brief Per-run node event records and clock pairing, live only while profiling is on. */
            Internal::Observability::ProfilingLedger profiling_ledger;

            /**
             * @brief Per-registration-index count of internal nodes sharing it.
             *
             * Computed once in capture_base_state(); sized to max(registration_index)+1
             * and indexed by registration_index. Sizes profiling_ledger's rows.
             */
            std::vector<std::uint32_t> profiling_multiplicities;

            /**
             * @brief Per-registration-index native-host flag, computed once alongside
             *        profiling_multiplicities.
             *
             * True where the index names a pure-CPU node whose event records are
             * raw steady times needing conversion at collect, rather than an
             * already-event-clock device record.
             */
            std::vector<bool> profiling_host_native;

            /** @brief Whether and how this plan runs as an in-order chain; set by the fluent Graph at build. */
            ChainPlan chain;
        };

    } // namespace Graph
} // namespace SushiRuntime
