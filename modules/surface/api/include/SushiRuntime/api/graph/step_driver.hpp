/**************************************************************************/
/* step_driver.hpp                                                        */
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
 * @file step_driver.hpp
 * @brief Declares StepDriver, the run entries a graph and a dynamic graph share.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/run_report_builder.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/observability/run_report.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Gives a graph its run entries and drives the step loop over the graph's plan.
         * @tparam Owner The graph; it befriends this driver and supplies the plan, the per-step
         *         hooks and the checks named in the README.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        template <class Owner>
        class StepDriver
        {
            public:
                /**
                 * @brief Runs the graph once, blocking until it completes.
                 * @return A RunReport summarising the run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run() { return run(std::size_t{1}); }

                /**
                 * @brief Runs the given number of successive steps and blocks until they finish.
                 * @return A RunReport aggregating the whole run.
                 * @throws Core::SushiException on any synchronous or device error.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport run(std::size_t steps)
                {
                    return unwrap(try_run(steps));
                }

                /**
                 * @brief Runs the graph once into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @throws Core::SushiException on any synchronous or device error.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport& run(Core::RunReport& reuse)
                {
                    return run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Runs @p steps successive steps into a report the caller owns.
                 *
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport& run(std::size_t steps, Core::RunReport& reuse)
                {
                    auto driven = try_run(steps, reuse);
                    if (!driven)
                        throw Core::SushiException(std::move(driven.error()));
                    return reuse;
                }

                /**
                 * @brief Runs steps until a wall-clock @p budget elapses.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Rep, class Period>
                Core::RunReport run_for(std::chrono::duration<Rep, Period> budget)
                {
                    const auto deadline = std::chrono::steady_clock::now() + budget;
                    return unwrap(drive([deadline](std::size_t k)
                    {
                        return k == 0 || std::chrono::steady_clock::now() < deadline;
                    }));
                }

                /**
                 * @brief Runs steps until @p done returns true.
                 * @param done Stop condition checked before each step; the run ends
                 *             the first time it returns true.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Predicate>
                Core::RunReport run_until(Predicate done)
                {
                    return unwrap(drive([done](std::size_t) { return !done(); }));
                }

                /**
                 * @brief Starts one step on the worker pool and returns without waiting for it.
                 * @pre The returned handle does not outlive this graph.
                 * @throws Core::SushiException If nothing is runnable, a recorded Buffer or State
                 *        was freed, the engine does not support asynchronous runs, or a run is
                 *        already in progress.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                RunHandle<Owner> run_async()
                {
                    Owner& self = owner();
                    if (!self.has_work())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                                Owner::K_NOTHING_TO_RUN});
                    if (async_step_pending_)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before starting another."});
                    if (self.has_dangling_resource())
                        throw Core::SushiException(Core::Error{Core::Errc::dangling_resource,
                                                                Owner::K_DANGLING_RESOURCE});

                    Execution::RuntimeContext& context = self.run_context();
                    auto& engine = context.get_execution_engine();
                    if (!engine.supports_asynchronous_run())
                        throw Core::SushiException(Core::Error{Core::Errc::unsupported,
                            "The active execution engine does not support run_async()."});

                    using clock = std::chrono::steady_clock;
                    detail::AsyncRunState state;

                    if (self.plan_is_stale())
                    {
                        const auto compile_started = clock::now();
                        self.rebuild_plan();
                        state.compile_ms = std::chrono::duration<double, std::milli>(
                            clock::now() - compile_started).count();
                    }
                    self.prepare_run();

                    // The pre-step order here matches the synchronous driver's.
                    ::SushiRuntime::Graph::CompiledGraph& plan = self.run_plan();
                    const detail::StepMachinery& steps = self.run_steps();
                    steps.resync();
                    engine.reset_cancellation();
                    if (plan.profiling)
                        engine.reset_profiling(plan);

                    state.before = engine.metrics();
                    const Observability::TransferCounters& transfers = context.transfers();
                    state.device_to_host_before = transfers.device_to_host();
                    state.host_to_device_before = transfers.host_to_device();
                    state.task_count = self.size();

                    steps.before_step();
                    self.rearm_plan();
                    state.started = clock::now();
                    engine.submit(plan);

                    async_step_pending_ = true;
                    return RunHandle<Owner>(&self, state);
                }

                /**
                 * @brief Requests cancellation of an in-progress run, from any thread, without
                 *        throwing.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void cancel() { owner().run_context().get_execution_engine().cancel(); }

                /**
                 * @brief Non-throwing multi-step run.
                 * @param steps Number of steps to run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error>
                try_run(std::size_t steps) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; }, steps,
                                 StepSync::AtEnd);
                }

                /**
                 * @brief Non-throwing single run into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(Core::RunReport& reuse) noexcept
                {
                    return try_run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Non-throwing multi-step run into a report the caller owns.
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(std::size_t steps, Core::RunReport& reuse) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; }, reuse, steps,
                                 StepSync::AtEnd);
                }

            protected:
                StepDriver() = default;
                StepDriver(const StepDriver&) = default;
                StepDriver(StepDriver&&) = default;
                StepDriver& operator=(const StepDriver&) = default;
                StepDriver& operator=(StepDriver&&) = default;
                ~StepDriver() = default;

            private:
                friend class RunHandle<Owner>;

                /** @brief Says whether a chained run may skip the host wait between steps. */
                enum class StepSync
                {
                    AtEnd,     /**< Fixed step count: submit back to back, one wait at the end. */
                    EveryStep  /**< The stop test may read results: wait after every step. */
                };

                /** @brief Returns the graph this driver runs. */
                Owner& owner() noexcept { return static_cast<Owner&>(*this); }

                /** @brief The engine a pending asynchronous step is running on. */
                Execution::IExecutionEngine& async_engine()
                {
                    return owner().run_context().get_execution_engine();
                }

                /**
                 * @brief Releases the in-flight marker for a step that ended badly.
                 *
                 * A task exception leaves the step over but unsettled: no report, no
                 * State advance. The graph must still become runnable again, or one
                 * throwing kernel would permanently refuse every later run.
                 */
                void discard_async_step() noexcept { async_step_pending_ = false; }

                /**
                 * @brief Closes out an asynchronous step once the engine reports it done.
                 * @param report The report to fill; reset before it is written.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void finish_async_step(const detail::AsyncRunState& state,
                                       Core::RunReport& report)
                {
                    const auto finished = std::chrono::steady_clock::now();
                    Owner& self = owner();
                    Execution::RuntimeContext& context = self.run_context();
                    auto& engine = context.get_execution_engine();
                    const detail::StepMachinery& steps = self.run_steps();
                    ::SushiRuntime::Graph::CompiledGraph& plan = self.run_plan();

                    async_step_pending_ = false;
                    steps.check_faults();
                    steps.after_step();

                    const auto after = engine.metrics();
                    detail::fill_run_report(
                        report, context, state.before, after, state.task_count, state.compile_ms,
                        std::chrono::duration<double, std::milli>(
                            finished - state.started).count());

                    report.cancelled = engine.is_cancelled();
                    const Observability::TransferCounters& transfers = context.transfers();
                    report.device_to_host_bytes =
                        transfers.device_to_host() - state.device_to_host_before;
                    report.host_to_device_bytes =
                        transfers.host_to_device() - state.host_to_device_before;

                    if (plan.profiling)
                        engine.collect_profiling(plan, report);
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return std::move(*r);
                }

                /**
                 * @brief Non-throwing run driver shared by every run/try_run entry.
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @return The aggregated RunReport, or a mapped Error on failure.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error>
                drive(KeepGoing keep_going, std::size_t expected_invocations = 1,
                      StepSync sync = StepSync::EveryStep) noexcept
                {
                    Core::RunReport report;
                    auto driven = drive(keep_going, report, expected_invocations, sync);
                    if (!driven)
                        return Core::make_error(std::move(driven.error()));
                    return std::move(report);
                }

                /**
                 * @brief Drives the run loop into a caller-owned report; every run entry goes
                 *        through here.
                 * @param keep_going Called with the completed step count before each step; true
                 *        continues.
                 * @param report The report to fill; reset before the run starts.
                 * @return A pointer to report on success, or a mapped Error.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport*, Core::Error>
                drive(KeepGoing keep_going, Core::RunReport& report,
                     std::size_t expected_invocations = 1,
                     StepSync sync = StepSync::EveryStep) noexcept
                {
                    // Reset before any early return, so a failed run shows no old numbers.
                    report.reset();
                    report.is_successful = false;

                    Owner& self = owner();
                    if (!self.has_work())
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            Owner::K_NOTHING_TO_RUN});
                    // A synchronous run must not re-arm nodes during an asynchronous step.
                    if (async_step_pending_)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before running this graph again."});
                    if (self.has_dangling_resource())
                        return Core::make_error(Core::Error{Core::Errc::dangling_resource,
                                                            Owner::K_DANGLING_RESOURCE});
                    try
                    {
                        drive_impl(keep_going, report, expected_invocations, sync);
                        return &report;
                    }
                    catch (const Core::SushiException& e)
                    {
                        return Core::make_error(e.error());
                    }
                    catch (const sycl::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (const std::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed, e.what()});
                    }
                    catch (...)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed,
                                                            "unknown exception"});
                    }
                }

                /**
                 * @brief Builds the plan if it is stale, runs the steps and fills the report.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class KeepGoing>
                void drive_impl(KeepGoing keep_going, Core::RunReport& report,
                                std::size_t expected_invocations, StepSync sync)
                {
                    using clock = std::chrono::steady_clock;
                    auto ms = [](clock::time_point a, clock::time_point b)
                    {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };

                    Owner& self = owner();
                    double compile_ms = 0.0;
                    if (self.plan_is_stale())
                    {
                        const auto c0 = clock::now();
                        self.rebuild_plan();
                        compile_ms = ms(c0, clock::now());
                    }
                    self.prepare_run();

                    ::SushiRuntime::Graph::CompiledGraph& plan = self.run_plan();
                    const detail::StepMachinery& steps = self.run_steps();

                    // State views point at their current and next buffers before the first step.
                    steps.resync();

                    Execution::RuntimeContext& context = self.run_context();
                    auto& engine = context.get_execution_engine();

                    // A cancel that ended an earlier run must not abort this one.
                    engine.reset_cancellation();

                    // Profiling accumulators are cleared only when the plan requests profiling.
                    const bool profiling = plan.profiling;
                    if (profiling)
                        engine.reset_profiling(plan, expected_invocations);

                    const auto before = engine.metrics();
                    const Observability::TransferCounters& transfers = context.transfers();
                    const std::size_t d2h_before = transfers.device_to_host();
                    const std::size_t h2d_before = transfers.host_to_device();
                    const auto t0 = clock::now();

                    const bool chained = self.runs_chained();
                    const std::size_t executed = chained
                        ? run_chained(keep_going, engine, sync)
                        : run_scheduled(keep_going, engine);

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = self.size() * executed;
                    detail::fill_run_report(report, context, before, after, total_tasks,
                                            compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();
                    report.layer = chained ? Core::ExecutionLayer::Chain
                                           : Core::ExecutionLayer::Scheduler;

                    // Only explicit transfers are counted; implicit USM migration is not.
                    report.device_to_host_bytes = transfers.device_to_host() - d2h_before;
                    report.host_to_device_bytes = transfers.host_to_device() - h2d_before;

                    // Per-node and per-worker timing, only when profiling is on.
                    if (profiling)
                        engine.collect_profiling(plan, report);
                }

                /**
                 * @brief Runs the steps on the work-stealing scheduler, waiting after each.
                 * @param keep_going Called with the count of completed steps before each step.
                 * @param engine     The context's execution engine.
                 * @return The number of steps run.
                 */
                template <class KeepGoing>
                std::size_t run_scheduled(KeepGoing& keep_going,
                                          Execution::IExecutionEngine& engine)
                {
                    Owner& self = owner();
                    ::SushiRuntime::Graph::CompiledGraph& plan = self.run_plan();
                    const detail::StepMachinery& steps = self.run_steps();
                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        // Live IO and late-bound controls are refreshed before the plan replays.
                        steps.before_step();

                        self.rearm_plan();
                        engine.execute(plan); // lvalue -> non-consuming replay
                        steps.check_faults();

                        // States advance after the step so the written next field becomes current.
                        steps.after_step();
                        ++executed;
                    }
                    return executed;
                }

                /**
                 * @brief Runs the steps as an in-order chain from this thread.
                 * @param sync AtEnd for a fixed step count, EveryStep when the stop test may read
                 *        results.
                 * @return The number of steps run.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class KeepGoing>
                std::size_t run_chained(KeepGoing& keep_going, Execution::IExecutionEngine& engine,
                                        StepSync sync)
                {
                    Owner& self = owner();
                    ::SushiRuntime::Graph::CompiledGraph& plan = self.run_plan();
                    const detail::StepMachinery& steps = self.run_steps();
                    Execution::ChainRun chain(self.run_context(), plan);
                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        steps.before_step();
                        self.rearm_plan();
                        chain.submit_step(static_cast<std::uint32_t>(executed));
                        if (sync == StepSync::EveryStep)
                        {
                            chain.wait();
                            steps.check_faults();
                        }
                        steps.after_step();
                        ++executed;
                    }
                    chain.finish();
                    steps.check_faults();
                    return executed;
                }

                /** @brief Tells whether a run_async() step has not been settled yet. */
                bool async_step_pending_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
