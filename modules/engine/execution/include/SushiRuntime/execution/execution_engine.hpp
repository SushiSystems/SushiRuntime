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
 * @brief Declares IExecutionEngine and ExecutionEngine, the runners of a CompiledGraph.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/observability/run_report.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
// runtime_settings.hpp is a leaf header, so this include creates no cycle.
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{

    namespace Graph { class OffloadCoordinator; class OffloadSink; }

    namespace Execution
    {
        class RuntimeContext;
        class ChainRun;

        /** @brief Names Graph::CompiledGraph under its former namespace. */
        using CompiledGraph = Graph::CompiledGraph;

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
        };

        /**
         * @brief Abstract interface for graph execution strategies.
         *
         * Separates "how to run a graph" from TaskGraph (which builds the DAG)
         * and RuntimeContext (which owns hardware). New strategies (dry-run,
         * profiling, replay) can be plugged in without touching either class.
         */
        class SUSHIRUNTIME_API IExecutionEngine
        {
        public:
            virtual ~IExecutionEngine() = default;

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
             * @brief Runs a compiled graph in place and blocks until completion.
             * @param graph The graph to run. Before it runs again, CompiledGraph::rearm() restores
             *        its nodes' per-run state from what capture_base_state() recorded.
             * @see modules/engine/execution/README.md
             */
            virtual void execute(CompiledGraph& graph) = 0;

            /**
             * @brief Runs a temporary compiled graph once and discards it.
             * @see modules/engine/execution/README.md
             */
            void execute(CompiledGraph&& graph);

            /**
             * @brief Returns whether this engine can start a run without blocking.
             * @return True when submit()/complete() are implemented.
             * @see modules/engine/execution/README.md
             */
            virtual bool supports_asynchronous_run() const { return false; }

            /**
             * @brief Starts a run and returns immediately, without waiting.
             * @pre Exactly one complete() follows, and the plan stays alive until it returns.
             * @throws Core::SushiException If the engine does not support this,
             *        is shut down, the graph has no roots, or a run is already
             *        in progress.
             * @see modules/engine/execution/README.md
             */
            virtual void submit(CompiledGraph& /*graph*/)
            {
                throw Core::SushiException(Core::Error{
                    Core::Errc::unsupported,
                    "This execution engine does not support asynchronous runs."});
            }

            /**
             * @brief Blocks until the run started by submit() has finished.
             * @pre submit() was called.
             * @throws Core::SushiException If the engine does not support this.
             * @throws The first exception a task threw during the run.
             * @see modules/engine/execution/README.md
             */
            virtual void complete()
            {
                throw Core::SushiException(Core::Error{
                    Core::Errc::unsupported,
                    "This execution engine does not support asynchronous runs."});
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
            virtual WorkerPoolInfo worker_pool() const { return {}; }

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
                                           Observability::RunReport& /*report*/) const {}

            /**
             * @brief Reports whether a run handed to this engine has not been collected yet.
             *
             * Default returns false, so engines that keep no such record and test doubles
             * need not implement it. Any thread may call it.
             * @return True from the start of a run until its completing call returns or throws.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] virtual bool run_in_progress() const noexcept { return false; }
        };

        /**
         * @brief Default work-stealing execution engine backed by TaskScheduler.
         *
         * Owns a single persistent TaskScheduler started once on construction.
         * Each execute() rearms the scheduler for a new graph run without
         * recreating worker threads. RuntimeContext::Impl shuts this engine down
         * before any queues or allocators are destroyed.
         */
        class SUSHIRUNTIME_API ExecutionEngine final : public IExecutionEngine
        {
        public:
            /**
             * @brief Constructs the engine and starts the persistent worker pool.
             * @param ctx The owning RuntimeContext. Must outlive this engine.
             */
            explicit ExecutionEngine(RuntimeContext& ctx);

            /**
             * @brief Shuts down the worker pool and waits for all threads to join.
             *
             * Called by RuntimeContext::Impl::~Impl() before any other resource
             * is destroyed, ensuring no dangling context references exist.
             */
            ~ExecutionEngine() override;

            // Non-copyable / non-movable – owns live threads.
            ExecutionEngine(const ExecutionEngine&) = delete;
            ExecutionEngine& operator=(const ExecutionEngine&) = delete;

            /**
             * @brief Executes a compiled graph synchronously on the persistent scheduler.
             * @see modules/engine/execution/README.md
             */
            void execute(CompiledGraph& graph) override;

            using IExecutionEngine::execute; // keep the rvalue convenience overload visible

            /** @brief This engine implements the submit()/complete() split. */
            bool supports_asynchronous_run() const override { return true; }

            /**
             * @brief Starts a run on the worker pool and returns immediately.
             * @param graph Graph to run in place; it must stay alive until the matching complete()
             *        returns.
             * @see modules/engine/execution/README.md
             */
            void submit(CompiledGraph& graph) override;

            /**
             * @brief Blocks until the submitted run finishes, then releases the engine.
             *
             * @throws The first exception a task threw during the run.
             */
            void complete() override;

            /** @brief Whether the submitted run's sentinel has already reported in. */
            bool run_finished() override;

            /** @brief Reads the live work-stealing counters from the scheduler. */
            ExecutionMetrics metrics() const override;

            /**
             * @brief Drains inflight work and stops the worker pool.
             *
             * Idempotent — safe to call multiple times.
             */
            void shutdown() override;

            /** @brief Requests cancellation of the active run via the scheduler. */
            void cancel() noexcept override;

            /** @brief Reports whether the active run has been cancelled. */
            bool is_cancelled() const override;

            /** @brief Clears the scheduler's cancellation flag for a fresh run. */
            void reset_cancellation() override;

            /**
             * @brief Turns the background thermal rebalancer on or off; safe to call between runs.
             * @see modules/engine/execution/README.md
             */
            void set_rebalancer_enabled(bool enabled) override;

            /** @brief Whether the background rebalancer is currently running. */
            bool rebalancer_enabled() const override;

            /** @brief The worker pool the scheduler actually built. */
            WorkerPoolInfo worker_pool() const override;

            /**
             * @brief Clears all profiling accumulators and resets the event ledger before a
             *        profiled run.
             * @param expected_invocations Per-node dispatch count this run expects.
             * @see modules/engine/execution/README.md
             */
            void reset_profiling(CompiledGraph& graph, std::size_t expected_invocations) override;

            /**
             * @brief Fills a report's profiling breakdown after a profiled run.
             * @param report The report to populate; left untouched for nodes that did not run.
             * @see modules/engine/execution/README.md
             */
            void collect_profiling(CompiledGraph& graph,
                                   Observability::RunReport& report) const override;

            /**
             * @brief Reports whether a scheduled or a chained run is in progress on this engine.
             * @return True inside execute(), between submit() and the end of complete(), and
             *         while a ChainRun built on this engine's context has not finished.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] bool run_in_progress() const noexcept override;

            /**
             * @brief Forwards to the scheduler's set_offload_coordinator().
             * @pre No run is in flight.
             * @param offload Not owned; null removes the installed one.
             */
            void install_offload(Graph::OffloadCoordinator* offload);

            /** @brief Returns the sink an offload coordinator hands its nodes back to. */
            [[nodiscard]] Graph::OffloadSink& offload_sink() noexcept;

        private:
            friend class ChainRun;

            /**
             * @brief Marks one chained run in progress.
             * @pre Exactly one end_chained_run() follows.
             */
            void begin_chained_run() noexcept;

            /** @brief Clears the mark one begin_chained_run() set. */
            void end_chained_run() noexcept;

            struct Impl;
            sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
