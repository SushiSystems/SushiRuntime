/****************************************************************/
/* execution_engine.hpp                                         */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/run_report.hpp>
#include <SushiRuntime/core/profiling_ledger.hpp>
#include <SushiRuntime/execution/chain_plan.hpp>
// WorkerPoolInfo is returned by worker_pool() below. runtime_settings.hpp is a
// leaf header (no SYCL, no runtime types), so including it here does not create
// a cycle with runtime_context.hpp, which includes this file.
#include <SushiRuntime/execution/runtime_settings.hpp>

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
             * @brief Sizes profiling_multiplicities and profiling_host_native from all_nodes.
             *
             * Shared by capture_base_state() (the static Graph) and DynamicGraph's
             * commit(), which has no compiled in-degree baseline of its own to
             * capture but needs this sizing whenever it (re)populates all_nodes.
             * Safe to call only once all_nodes carries its final registration
             * indices for this compose.
             */
            void capture_profiling_state();

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

            /** @brief Whether this plan dispatches its device nodes on the profiling queue. */
            bool profiling = false;

            /** @brief Per-run node event records and clock pairing, live only while profiling is on. */
            Core::ProfilingLedger profiling_ledger;

            /**
             * @brief Per-registration-index count of internal nodes sharing it (F5).
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
             *
             * Returns nothing. It used to return a default-constructed sycl::event
             * that carried no information -- the run is already finished when this
             * returns, so there was never a hardware event to chain on, and the
             * docs said so. Constructing one is not free: on oneAPI's libsycl
             * sycl::event{} heap-allocates, which put one allocation per tick on
             * the steady-state loop that run(RunReport&) promises will not allocate.
             */
            virtual void execute(CompiledGraph& graph) = 0;

            /**
             * @brief One-shot convenience: runs a temporary compiled graph.
             *
             * Binds rvalues (e.g. execute(graph.compile())) and delegates to the
             * in-place overload. The moved-in graph is destroyed when the call
             * returns, so this is the right entry point for a single run.
             *
             * @param graph A compiled graph to run once and discard.
             */
            void execute(CompiledGraph&& graph);

            /**
             * @brief Whether this engine can start a run without blocking.
             *
             * Reported rather than assumed so a strategy that has no meaningful
             * split between starting and finishing a run (a dry-run engine, a
             * replay engine, a test double) is not forced to fake one. False by
             * default; callers must check before using submit()/complete().
             *
             * @return True when submit()/complete() are implemented.
             */
            virtual bool supports_asynchronous_run() const { return false; }

            /**
             * @brief Starts a run and returns immediately, without waiting.
             *
             * The half of execute() that hands work to the scheduler. Everything
             * execute() does up to blocking happens here, so the work proceeds on
             * the worker pool while the caller does something else — extracting
             * the previous step's state for a renderer, say. Every submit() must
             * be paired with exactly one complete(); until it is, the engine
             * rejects another run and the compiled plan must stay alive.
             *
             * @param graph The compiled graph to run in place (not consumed).
             * @throws Core::SushiException If the engine does not support this,
             *         is shut down, the graph has no roots, or a run is already
             *         in progress.
             */
            virtual void submit(CompiledGraph& /*graph*/)
            {
                throw Core::SushiException(Core::Error{
                    Core::Errc::unsupported,
                    "This execution engine does not support asynchronous runs."});
            }

            /**
             * @brief Blocks until the run started by submit() has finished.
             *
             * The other half of execute(): waits for the completion sentinel,
             * drains the pool, releases the engine for the next run, and
             * rethrows a task exception exactly as execute() would. Calling it
             * without a matching submit() is a programming error.
             *
             * @throws Core::SushiException If the engine does not support this.
             * @throws The first exception a task threw during the run.
             */
            virtual void complete()
            {
                throw Core::SushiException(Core::Error{
                    Core::Errc::unsupported,
                    "This execution engine does not support asynchronous runs."});
            }

            /**
             * @brief Whether a submitted run has already finished, without waiting.
             *
             * Lets a caller decide whether waiting would block — the difference
             * between overlapping more work and stalling. Meaningless (and true)
             * when no run is in flight.
             *
             * @return True once the run's completion sentinel has reported in.
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
             * @brief The worker pool this engine actually built.
             *
             * Reports the applied count and core placement, which differ from the
             * requested RuntimeSettings when the range did not fit the machine.
             * Default is an empty pool so engines without worker threads (and test
             * doubles) need not implement it.
             *
             * @return The pool's size, first bound core, and pin policy.
             */
            virtual WorkerPoolInfo worker_pool() const { return {}; }

            /**
             * @brief Clears profiling accumulators before a profiled run.
             *
             * Default no-op so engines that gather no timing (and test doubles) need
             * not implement it.
             * @param graph The compiled plan whose counters reset.
             * @param expected_invocations Per-node dispatch count the caller expects
             *        this run to reach; sizes the event ledger before dispatch starts.
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
                                           Core::RunReport& /*report*/) const {}
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
             */
            void execute(CompiledGraph& graph) override;

            using IExecutionEngine::execute; // keep the rvalue convenience overload visible

            /** @brief This engine implements the submit()/complete() split. */
            bool supports_asynchronous_run() const override { return true; }

            /**
             * @brief Starts a run on the worker pool and returns immediately.
             *
             * execute() is exactly submit() followed by complete(); the two exist
             * separately so a caller can put its own work between them. See
             * IExecutionEngine::submit for the pairing contract.
             *
             * @param graph Compiled graph to run in place (not consumed); it must
             *              stay alive until the matching complete() returns.
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
            void cancel() override;

            /** @brief Reports whether the active run has been cancelled. */
            bool is_cancelled() const override;

            /** @brief Clears the scheduler's cancellation flag for a fresh run. */
            void reset_cancellation() override;

            /**
             * @brief Turns the background thermal rebalancer on or off.
             *
             * The rebalancer migrates tasks between workers for throughput, but it
             * is a background thread doing work mid-run and a source of run-to-run
             * nondeterminism. Disable it for a low-jitter real-time frame or a
             * deterministic/replay run; leave it on (the default) for batch
             * throughput. Safe to call between runs.
             *
             * @param enabled True to run the rebalancer, false to stop it.
             */
            void set_rebalancer_enabled(bool enabled) override;

            /** @brief Whether the background rebalancer is currently running. */
            bool rebalancer_enabled() const override;

            /** @brief The worker pool the scheduler actually built. */
            WorkerPoolInfo worker_pool() const override;

            /**
             * @brief Clears all profiling accumulators before a profiled run.
             *
             * Zeroes every node's per-run timing in @p graph and the scheduler's
             * per-worker time buckets, so the resulting report covers only the run
             * about to start. The driver calls this only when profiling is on.
             *
             * Also resets the plan's event ledger, sizing it to @p expected_invocations
             * per node (or the previous run's high-water mark, whichever is larger),
             * so capture never allocates once a shape has been seen.
             *
             * @param graph The compiled plan whose node counters are reset.
             * @param expected_invocations Per-node dispatch count this run expects.
             */
            void reset_profiling(CompiledGraph& graph, std::size_t expected_invocations) override;

            /**
             * @brief Fills a report's profiling breakdown after a profiled run.
             *
             * Reads each node's accumulated device/host time and invocation count
             * into @p report.node_timings (skipping nodes that never ran), the
             * per-worker time split into @p report.worker_timings, and derives
             * scheduling_overhead_ms from the workers' completion-polling time.
             *
             * @param graph  The compiled plan whose node counters are read.
             * @param report The report to populate; left untouched for nodes that
             *               did not run.
             */
            void collect_profiling(CompiledGraph& graph, Core::RunReport& report) const override;

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
