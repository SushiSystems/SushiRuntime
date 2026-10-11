/**************************************************************************/
/* scheduler_execution_engine.hpp                                         */
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
 * @file scheduler_execution_engine.hpp
 * @brief Declares SchedulerExecutionEngine, the engine that runs a graph on the task scheduler.
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
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Default work-stealing execution engine backed by TaskScheduler.
         *
         * Owns a single persistent TaskScheduler started once on construction.
         * Each execute() rearms the scheduler for a new graph run without
         * recreating worker threads. RuntimeContext::Impl shuts this engine down
         * before any queues or allocators are destroyed.
         */
        class SUSHIRUNTIME_API SchedulerExecutionEngine final : public ExecutionEngine
        {
        public:
            /**
             * @brief Constructs the engine and starts the persistent worker pool.
             * @param context The owning RuntimeContext. Must outlive this engine.
             */
            explicit SchedulerExecutionEngine(RuntimeContext& context);

            /**
             * @brief Shuts down the worker pool and waits for all threads to join.
             *
             * Called by RuntimeContext::Impl::~Impl() before any other resource
             * is destroyed, ensuring no dangling context references exist.
             */
            ~SchedulerExecutionEngine() override;

            // Non-copyable / non-movable – owns live threads.
            SchedulerExecutionEngine(const SchedulerExecutionEngine&) = delete;
            SchedulerExecutionEngine& operator=(const SchedulerExecutionEngine&) = delete;

            /**
             * @brief Executes a compiled graph synchronously on the persistent scheduler.
             * @return What submit() refused with, or what complete() returns.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] Result<void> execute(CompiledGraph& graph) override;

            /**
             * @brief Hands the graph's nodes to the installed offload coordinator, if any.
             * @pre No node of graph has been submitted.
             * @see modules/engine/execution/README.md
             */
            void prepare(CompiledGraph& graph) override;

            using ExecutionEngine::execute; // keep the rvalue convenience overload visible

            /** @brief This engine implements the submit()/complete() split. */
            bool supports_asynchronous_run() const override { return true; }

            /**
             * @brief Starts a run on the worker pool and returns immediately.
             * @param graph Graph to run in place; it must stay alive until the matching complete()
             *        returns.
             * @return Nothing, or InvalidState when the engine is shut down or a run is already
             *         in progress, and InvalidGraph when the graph has no roots.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] Result<void> submit(CompiledGraph& graph) override;

            /**
             * @brief Blocks until the submitted run finishes, then releases the engine.
             * @return Nothing, or the first failure of the run: what a task reported, or an
             *         error a device queue held for its asynchronous handler.
             * @see modules/engine/execution/README.md
             */
            [[nodiscard]] Result<void> complete() override;

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
            WorkerPool worker_pool() const override;

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
                                   Internal::Observability::RunReport& report) const override;

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
            void install_offload(Internal::Graph::OffloadCoordinator* offload);

            /** @brief Returns the sink an offload coordinator hands its nodes back to. */
            [[nodiscard]] Internal::Graph::OffloadSink& offload_sink() noexcept;

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
    } // namespace Internal::Execution
} // namespace SushiRuntime
