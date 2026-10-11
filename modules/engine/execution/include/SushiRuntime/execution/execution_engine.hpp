/**************************************************************************/
/* execution_engine.hpp                                                   */
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
 * @file execution_engine.hpp
 * @brief Declares ExecutionEngine, the interface that runs a CompiledGraph.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/observability/run_report.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
// runtime_settings.hpp is a leaf header, so this include creates no cycle.
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{

    namespace Internal::Graph { class OffloadCoordinator; class OffloadSink; }

    namespace Internal::Execution
    {
        class RuntimeContext;
        class ChainRun;

        /** @brief Names Internal::Graph::CompiledGraph under its former namespace. */
        using CompiledGraph = Internal::Graph::CompiledGraph;

        /**
         * @brief Cumulative scheduler counters, snapshotted to report a run.
         *
         * Counts are lifetime totals of the persistent scheduler; callers take
         * a before/after difference to attribute a single run. Steal counts split
         * by NUMA distance (node-local vs cross-node), which the fluent RunReport
         * surfaces as intra/inter-NUMA steals.
         */
        struct ExecutionMetrics
        {
            std::size_t local_steals = 0;       /**< Successful steals from a node-local victim. */
            std::size_t remote_steals = 0;      /**< Successful steals that crossed NUMA nodes. */
            std::size_t tasks_completed = 0;    /**< Tasks completed across all workers. */
            std::size_t tasks_offloaded = 0;    /**< Dispatches an offload coordinator took. */
        };

        /**
         * @brief Abstract interface for graph execution strategies.
         *
         * Separates "how to run a graph" from TaskGraph (which builds the DAG)
         * and RuntimeContext (which owns hardware). New strategies (dry-run,
         * profiling, replay) can be plugged in without touching either class.
         */
        class SUSHIRUNTIME_API ExecutionEngine
        {
        public:
            virtual ~ExecutionEngine() = default;

            /**
             * @brief Cumulative scheduler counters for this engine.
             *
             * Default returns zeros so non-instrumented engines and test doubles
             * need not implement it. The work-stealing engine overrides it.
             *
             * @return Lifetime totals; diff two snapshots to measure one run.
             */
            virtual ExecutionMetrics metrics() const { return {}; }

            /**
             * @brief Readies a freshly compiled graph for this engine, once, before it runs.
             *
             * Does nothing by default. The work-stealing engine hands the nodes to the
             * installed offload coordinator, which marks those a worker may run.
             * @pre No node of graph has been submitted.
             * @see modules/engine/execution/README.md
             */
            virtual void prepare(CompiledGraph& /*graph*/) {}

            /**
             * @brief Runs a compiled graph in place and blocks until completion.
             * @param graph The graph to run. Before it runs again, CompiledGraph::rearm() restores
             *        its nodes' per-run state from what capture_base_state() recorded.
             * @return Nothing, or why the run did not start or what its first failed task
             *         reported.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] virtual Result<void> execute(CompiledGraph& graph) = 0;

            /**
             * @brief Runs a temporary compiled graph once and discards it.
             * @return What execute() returns for the graph.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] Result<void> execute(CompiledGraph&& graph);

            /**
             * @brief Returns whether this engine can start a run without blocking.
             * @return True when submit()/complete() are implemented.
             * @see modules/engine/execution/README.md
             */
            virtual bool supports_asynchronous_run() const { return false; }

            /**
             * @brief Starts a run and returns immediately, without waiting.
             * @pre supports_asynchronous_run() is true. Exactly one complete() follows a
             *      submit() that returned a value, and the plan stays alive until it returns.
             * @return Nothing, or InvalidState when the engine is shut down or a run is already
             *         in progress, and InvalidGraph when the graph has no roots.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] virtual Result<void> submit(CompiledGraph& /*graph*/)
            {
                SR_FATAL("ExecutionEngine::submit(): this engine does not support asynchronous "
                         "runs; ask supports_asynchronous_run() first");
            }

            /**
             * @brief Blocks until the run started by submit() has finished.
             * @pre supports_asynchronous_run() is true and submit() returned a value.
             * @return Nothing, or the first failure of the run.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] virtual Result<void> complete()
            {
                SR_FATAL("ExecutionEngine::complete(): this engine does not support "
                         "asynchronous runs; ask supports_asynchronous_run() first");
            }

            /**
             * @brief Returns whether a submitted run has already finished, without waiting.
             * @return True once the completion sentinel has reported in, or when no run is in
             *        flight.
             * @see modules/engine/execution/README.md
             */
            virtual bool run_finished() { return true; }

            /**
             * @brief Signals the engine to stop accepting new work and drain active tasks.
             *
             * Safe to call from any thread. Blocks until all inflight work is complete.
             */
            virtual void shutdown() = 0;

            /**
             * @brief Requests cancellation of the active run. Thread-safe.
             *
             * The in-flight execute() drains remaining work and returns; a multi-step
             * driver then stops launching further steps. Default no-op so test
             * doubles need not implement it.
             */
            virtual void cancel() noexcept {}

            /**
             * @brief Reports whether the current/last run was cancelled.
             * @return True between a cancel() and the next reset_cancellation().
             */
            virtual bool is_cancelled() const { return false; }

            /**
             * @brief Clears cancellation state at the start of a fresh run.
             *
             * Called by the driver before the first step so a prior cancelled run
             * does not poison the next. Default no-op.
             */
            virtual void reset_cancellation() {}

            /**
             * @brief Turns the background thermal rebalancer on or off, if any.
             *
             * Default no-op so engines without a rebalancer (and test doubles) need
             * not implement it. @param enabled True to run it, false to stop it.
             */
            virtual void set_rebalancer_enabled(bool /*enabled*/) {}

            /** @brief Whether a background rebalancer is running. Default false. */
            virtual bool rebalancer_enabled() const { return false; }

            /**
             * @brief Returns the worker pool this engine built; an empty pool by default.
             * @see modules/engine/execution/README.md
             */
            virtual WorkerPool worker_pool() const { return {}; }

            /**
             * @brief Clears profiling accumulators before a profiled run; does nothing by default.
             * @param expected_invocations Per-node dispatch count the caller expects; sizes the
             *        ledger.
             * @see modules/engine/execution/README.md
             */
            virtual void reset_profiling(CompiledGraph& /*graph*/,
                                         std::size_t /*expected_invocations*/ = 1) {}

            /**
             * @brief Fills a report's profiling breakdown after a profiled run.
             *
             * Default no-op. @param graph The compiled plan read for timing.
             * @param report The report whose profiling rows are populated.
             */
            virtual void collect_profiling(CompiledGraph& /*graph*/,
                                           Internal::Observability::RunReport& /*report*/) const {}

            /**
             * @brief Reports whether a run handed to this engine has not been collected yet.
             *
             * Default returns false, so engines that keep no such record and test doubles
             * need not implement it. Any thread may call it.
             * @return True from the start of a run until its completing call returns.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] virtual bool run_in_progress() const noexcept { return false; }
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
