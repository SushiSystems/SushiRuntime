/**************************************************************************/
/* execution_engine.hpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
// CompiledGraph holds sushi_ptr<Graph::Node> members and is dllexport'd
// (SUSHIRUNTIME_API). On Windows, exporting a class forces its implicit special
// members to be instantiated at the class definition, which requires Node to be
// complete here — a forward declaration triggers "member access into incomplete
// type" under clang. Including the definition keeps the build valid on Windows
// and is a no-op on platforms where the class is not exported (e.g. Linux).
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{

#ifdef SR_DISTRIBUTED
    namespace Scheduler { class TaskScheduler; }
#endif

    namespace Execution
    {
        class RuntimeContext;

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
            std::vector<SushiRuntime::Graph::Node*> roots;

            /** @brief Sentinel node injected at the end of the graph. */
            sushi_ptr<SushiRuntime::Graph::Node> sentinel;

            /** * @brief Owns all nodes to keep them alive during execution.
             * This prevents Use-After-Free bugs since internal dependencies rely on raw pointers.
             */
            std::vector<sushi_ptr<SushiRuntime::Graph::Node>> all_nodes;

            /**
             * @brief Snapshots each node's compiled in-degree so the plan can replay.
             *
             * execute() consumes a node's in_degree as it releases successors, so a
             * second run needs the original counts restored first. Call this once
             * after compile(); rearm() then restores from the snapshot before every
             * subsequent run. Keeping this here (next to Node) instead of in the
             * fluent API is what lets the public API stop reaching into Node's
             * scheduler-internal fields.
             */
            void capture_base_state();

            /**
             * @brief Restores every node's per-run state to its compiled baseline.
             *
             * Resets in_degree (nodes + sentinel) from the snapshot capture_base_state()
             * took, and clears any distributed offload bookkeeping, so the cached plan
             * is ready to run again. Safe to call only after capture_base_state().
             */
            void rearm() noexcept;

            /** @brief Per-node compiled in-degree baseline, filled by capture_base_state(). */
            std::vector<std::int32_t> base_in_degree;
            /** @brief Sentinel's compiled in-degree baseline. */
            std::int32_t sentinel_in_degree = 0;
        };

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
             *
             * Non-consuming: the engine reads @p graph's roots and sentinel and
             * resets/re-arms its scheduler for the round, but does not take
             * ownership of the nodes. This is what makes replay possible — the
             * caller keeps one CompiledGraph and runs it many times, resetting node
             * state between runs. The one-shot rvalue overload below delegates here.
             *
             * @param graph The compiled graph to run; left intact (nodes' per-run
             *              state such as in_degree is consumed and must be reset by
             *              the caller before the next run).
             * @return sycl::event Currently always sycl::event{}. Do not chain SYCL
             *         dependencies on this value.
             */
            virtual sycl::event execute(CompiledGraph& graph) = 0;

            /**
             * @brief One-shot convenience: runs a temporary compiled graph.
             *
             * Binds rvalues (e.g. execute(graph.compile())) and delegates to the
             * in-place overload. The moved-in graph is destroyed when the call
             * returns, so this is the right entry point for a single run.
             *
             * @param graph A compiled graph to run once and discard.
             * @return sycl::event As for the in-place overload.
             */
            sycl::event execute(CompiledGraph&& graph);

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
            virtual void cancel() {}

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
             * @brief Executes a compiled graph synchronously.
             *
             * Injects all root nodes into the persistent scheduler, re-arms the
             * running flag for this execution round, then blocks on the sentinel's
             * completion promise. Does NOT recreate any threads.
             *
             * @param graph Compiled graph to run in place (not consumed).
             * @return sycl::event Always sycl::event{} — the sentinel runs inline
             *         (not via a SYCL submission) so there is no hardware event to
             *         return. Do not chain SYCL dependencies on this value.
             */
            sycl::event execute(CompiledGraph& graph) override;

            using IExecutionEngine::execute; // keep the rvalue convenience overload visible

            /** @brief Reads the live work-stealing counters from the scheduler. */
            ExecutionMetrics metrics() const override;

            /**
             * @brief Drains inflight work and stops the worker pool.
             *
             * Idempotent — safe to call multiple times.
             */
            void shutdown() override;

            /** @brief Requests cancellation of the active run via the scheduler. */
            void cancel() override;

            /** @brief Reports whether the active run has been cancelled. */
            bool is_cancelled() const override;

            /** @brief Clears the scheduler's cancellation flag for a fresh run. */
            void reset_cancellation() override;

#ifdef SR_DISTRIBUTED
            /** @brief Access to the persistent scheduler, so Master mode can attach the coordinator. */
            SushiRuntime::Scheduler::TaskScheduler& scheduler_ref();
#endif

        private:
            struct Impl;
            sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
