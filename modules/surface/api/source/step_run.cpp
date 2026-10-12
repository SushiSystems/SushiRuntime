/**************************************************************************/
/* step_run.cpp                                                           */
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
 * @file step_run.cpp
 * @brief Implements the step loops and the report of a run over a built plan.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/step_run.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/report/run_report.hpp>

#include "plan_access.hpp"
#include "run_report_builder.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            using Internal::Execution::ExecutionEngine;
            using Internal::Execution::ExecutionMetrics;
            using Internal::Execution::RuntimeContext;
            using Internal::Graph::CompiledGraph;

            /** @brief Copies the engine's counters into the form an AsyncRunState keeps. */
            RunCounters counters_of(const ExecutionMetrics& metrics) noexcept
            {
                RunCounters counters;
                counters.local_steals = metrics.local_steals;
                counters.remote_steals = metrics.remote_steals;
                counters.tasks_completed = metrics.tasks_completed;
                counters.tasks_offloaded = metrics.tasks_offloaded;
                return counters;
            }

            /** @brief Restores the engine's counters from the form an AsyncRunState keeps. */
            ExecutionMetrics metrics_of(const RunCounters& counters) noexcept
            {
                ExecutionMetrics metrics;
                metrics.local_steals = counters.local_steals;
                metrics.remote_steals = counters.remote_steals;
                metrics.tasks_completed = counters.tasks_completed;
                metrics.tasks_offloaded = counters.tasks_offloaded;
                return metrics;
            }

            /**
             * @brief Clears every fault record, so a run starts with none of the run before.
             * @pre No kernel of the plan is running.
             */
            void discard_faults(const StepMachinery& steps)
            {
                // take_fault() clears the one record it returns, so the loop ends on none.
                while (steps.take_fault())
                {

                }
            }

            /**
             * @brief Runs the steps on the work-stealing scheduler, waiting after each.
             * @param keep_going Called with the count of completed steps before each step.
             * @return The number of steps run, or the fault a step recorded.
             */
            Result<std::size_t> run_scheduled(const StepTarget& target, StepPredicate keep_going,
                                              ExecutionEngine& engine)
            {
                CompiledGraph& plan = *engine_plan(target.plan);
                const StepMachinery& steps = *target.steps;
                std::size_t executed = 0;
                while (keep_going(executed) && !engine.is_cancelled())
                {
                    // The caller's hooks run first, so a late-bound control reads what they set.
                    target.hooks->run(executed);
                    steps.before_step();

                    target.rearm(target.owner);
                    Result<void> ran = engine.execute(plan);
                    if (!ran)
                        return make_error(std::move(ran.error()));
                    if (std::optional<Error> fault = steps.take_fault())
                        return make_error(std::move(*fault));

                    // States advance after the step so the written next field becomes current.
                    steps.after_step();
                    ++executed;
                }
                return executed;
            }

            /**
             * @brief Runs the steps as an in-order chain from this thread.
             * @return The number of steps run, or the fault a step recorded.
             * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
             */
            Result<std::size_t> run_chained(const StepTarget& target, StepPredicate keep_going,
                                            RuntimeContext& context, ExecutionEngine& engine,
                                            StepSync sync)
            {
                CompiledGraph& plan = *engine_plan(target.plan);
                const StepMachinery& steps = *target.steps;
                Internal::Execution::ChainRun chain(context, plan);
                const bool wait_every_step = (sync == StepSync::EveryStep);
                const bool advance_unchecked = !wait_every_step;
                std::size_t executed = 0;
                while (keep_going(executed) && !engine.is_cancelled())
                {
                    // The step queued before this one has not advanced the States yet.
                    if (advance_unchecked && executed != 0)
                        steps.after_step();

                    target.hooks->run(executed);
                    steps.before_step();
                    target.rearm(target.owner);
                    Result<void> submitted =
                        chain.submit_step(static_cast<std::uint32_t>(executed));
                    if (!submitted)
                        return make_error(std::move(submitted.error()));
                    if (wait_every_step)
                    {
                        Result<void> waited = chain.wait();
                        if (!waited)
                            return make_error(std::move(waited.error()));
                        if (std::optional<Error> fault = steps.take_fault())
                            return make_error(std::move(*fault));
                        steps.after_step();
                    }
                    ++executed;
                }
                Result<void> finished = chain.finish();
                if (!finished)
                    return make_error(std::move(finished.error()));
                if (std::optional<Error> fault = steps.take_fault())
                    return make_error(std::move(*fault));

                // With one wait at the end the last step advances the States once checked.
                if (advance_unchecked && executed != 0)
                    steps.after_step();
                return executed;
            }
        } // namespace

        bool profiling_default(const ContextHandle& context) noexcept
        {
            return Internal::ContextAccess::context(context).settings().profiling;
        }

        Result<void> run_steps(const StepTarget& target, StepPredicate keep_going,
                               RunReport& report, double compile_ms,
                               std::size_t expected_invocations, StepSync sync)
        {
            using clock = std::chrono::steady_clock;

            CompiledGraph& plan = *engine_plan(target.plan);
            const StepMachinery& steps = *target.steps;

            // A run that failed returned before it read the records of its last step.
            discard_faults(steps);

            // State views point at their current and next buffers before the first step.
            steps.resync();

            RuntimeContext& context = Internal::ContextAccess::context(*target.context);
            ExecutionEngine& engine = context.execution_engine();
            engine.reset_cancellation();

            // Profiling accumulators are cleared only when the plan requests profiling.
            const bool profiling = plan.profiling;
            if (profiling)
                engine.reset_profiling(plan, expected_invocations);

            const ExecutionMetrics before = engine.metrics();
            const Internal::Observability::TransferCounters& transfers = context.transfers();
            const std::size_t d2h_before = transfers.device_to_host();
            const std::size_t h2d_before = transfers.host_to_device();
            const auto t0 = clock::now();

            // A hook may write what a kernel reads, so each step ends before the next hook.
            const StepSync hooked_sync = target.hooks->empty() ? sync : StepSync::EveryStep;
            const bool chained = target.may_chain && plan.chain.chainable &&
                                 !context.settings().force_scheduler;
            const Result<std::size_t> stepped = chained
                ? run_chained(target, keep_going, context, engine, hooked_sync)
                : run_scheduled(target, keep_going, engine);
            if (!stepped)
                return make_error(stepped.error());
            const std::size_t executed = *stepped;

            const auto t1 = clock::now();
            const ExecutionMetrics after = engine.metrics();

            const std::size_t total_tasks = target.node_count * executed;
            fill_run_report(report, context, before, after, total_tasks, compile_ms,
                            std::chrono::duration<double, std::milli>(t1 - t0).count());
            report.cancelled = engine.is_cancelled();
            report.layer = chained ? ExecutionLayer::Chain : ExecutionLayer::Scheduler;

            // Only explicit transfers are counted; implicit USM migration is not.
            report.device_to_host_bytes = transfers.device_to_host() - d2h_before;
            report.host_to_device_bytes = transfers.host_to_device() - h2d_before;

            // Per-node and per-worker timing, only when profiling is on.
            if (profiling)
                engine.collect_profiling(plan, report);
            return {};
        }

        Result<void> submit_step(const StepTarget& target, AsyncRunState& state)
        {
            RuntimeContext& context = Internal::ContextAccess::context(*target.context);
            ExecutionEngine& engine = context.execution_engine();
            CompiledGraph& plan = *engine_plan(target.plan);
            const StepMachinery& steps = *target.steps;

            // A step that failed returned before it read its records.
            discard_faults(steps);

            steps.resync();
            engine.reset_cancellation();
            if (plan.profiling)
                engine.reset_profiling(plan);

            state.before = counters_of(engine.metrics());
            const Internal::Observability::TransferCounters& transfers = context.transfers();
            state.device_to_host_before = transfers.device_to_host();
            state.host_to_device_before = transfers.host_to_device();
            state.task_count = target.node_count;

            target.hooks->run(0);
            steps.before_step();
            target.rearm(target.owner);
            state.started = std::chrono::steady_clock::now();
            return engine.submit(plan);
        }

        Result<void> finish_step(const StepTarget& target, const AsyncRunState& state,
                                 RunReport& report)
        {
            const auto finished = std::chrono::steady_clock::now();
            RuntimeContext& context = Internal::ContextAccess::context(*target.context);
            ExecutionEngine& engine = context.execution_engine();
            const StepMachinery& steps = *target.steps;
            CompiledGraph& plan = *engine_plan(target.plan);

            if (std::optional<Error> fault = steps.take_fault())
                return make_error(std::move(*fault));
            steps.after_step();

            const ExecutionMetrics after = engine.metrics();
            fill_run_report(report, context, metrics_of(state.before), after, state.task_count,
                            state.compile_ms,
                            std::chrono::duration<double, std::milli>(
                                finished - state.started).count());

            report.cancelled = engine.is_cancelled();
            const Internal::Observability::TransferCounters& transfers = context.transfers();
            report.device_to_host_bytes =
                transfers.device_to_host() - state.device_to_host_before;
            report.host_to_device_bytes =
                transfers.host_to_device() - state.host_to_device_before;

            if (plan.profiling)
                engine.collect_profiling(plan, report);
            return {};
        }

        Result<void> complete_step(const ContextHandle& context)
        {
            return Internal::ContextAccess::context(context).execution_engine().complete();
        }

        bool step_finished(const ContextHandle& context)
        {
            return Internal::ContextAccess::context(context).execution_engine().run_finished();
        }

        bool supports_asynchronous_run(const ContextHandle& context)
        {
            return Internal::ContextAccess::context(context)
                .execution_engine().supports_asynchronous_run();
        }

        bool run_in_progress(const ContextHandle& context) noexcept
        {
            return Internal::ContextAccess::context(context).execution_engine().run_in_progress();
        }

        void cancel_run(const ContextHandle& context) noexcept
        {
            Internal::ContextAccess::context(context).execution_engine().cancel();
        }
    } // namespace Detail
} // namespace SushiRuntime
