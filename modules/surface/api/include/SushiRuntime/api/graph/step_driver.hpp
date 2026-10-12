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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/detail/before_step_hooks.hpp>
#include <SushiRuntime/api/detail/callable_checks.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/detail/step_run.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/report/run_report.hpp>

namespace SushiRuntime
{
    namespace Detail
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
                 * @return The RunReport, or the Error that ended the run; a failed run of one
                 *         step has advanced no State.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Result<RunReport, Error> run() noexcept
                {
                    return run(std::size_t{1});
                }

                /**
                 * @brief Runs the given number of successive steps and blocks until they finish.
                 * @return The RunReport, or the Error that ended the run. A failed run has
                 *         advanced the States once per step before the failing one; on the chain
                 *         a device fault of any step shows at the end, with every step but the
                 *         last advanced.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Result<RunReport, Error>
                run(std::size_t steps) noexcept
                {
                    return drive(for_steps(steps), steps, Detail::StepSync::AtEnd);
                }

                /**
                 * @brief Runs the graph once into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to reuse, or the Error that ended the run.
                 */
                [[nodiscard]] Result<RunReport*, Error>
                run(RunReport& reuse) noexcept
                {
                    return run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Runs the given number of steps into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to reuse, or the Error that ended the run.
                 */
                [[nodiscard]] Result<RunReport*, Error>
                run(std::size_t steps, RunReport& reuse) noexcept
                {
                    return drive(for_steps(steps), reuse, steps, Detail::StepSync::AtEnd);
                }

                /**
                 * @brief Runs steps until a wall-clock budget elapses.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @return The RunReport, or the Error that ended the run.
                 */
                template <class Rep, class Period>
                [[nodiscard]] Result<RunReport, Error>
                run_for(std::chrono::duration<Rep, Period> budget) noexcept
                {
                    return drive(within(budget), 1, Detail::StepSync::EveryStep);
                }

                /**
                 * @brief Runs steps until a wall-clock budget elapses, into a report the caller
                 *        owns.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to reuse, or the Error that ended the run.
                 */
                template <class Rep, class Period>
                [[nodiscard]] Result<RunReport*, Error>
                run_for(std::chrono::duration<Rep, Period> budget,
                            RunReport& reuse) noexcept
                {
                    return drive(within(budget), reuse, 1, Detail::StepSync::EveryStep);
                }

                /**
                 * @brief Runs steps until done returns true.
                 * @tparam Predicate Move-constructible without throwing.
                 * @param done Stop test called before each step; an exception it throws ends the
                 *        run as KernelFailed.
                 * @return The RunReport, or the Error that ended the run.
                 */
                template <class Predicate>
                [[nodiscard]] Result<RunReport, Error>
                run_until(Predicate done) noexcept
                {
                    require_nothrow_move<Predicate>();
                    return drive(until(std::move(done)), 1, Detail::StepSync::EveryStep);
                }

                /**
                 * @brief Runs steps until done returns true, into a report the caller owns.
                 * @tparam Predicate Move-constructible without throwing.
                 * @param done Stop test called before each step; an exception it throws ends the
                 *        run as KernelFailed.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to reuse, or the Error that ended the run.
                 */
                template <class Predicate>
                [[nodiscard]] Result<RunReport*, Error>
                run_until(Predicate done, RunReport& reuse) noexcept
                {
                    require_nothrow_move<Predicate>();
                    return drive(until(std::move(done)), reuse, 1, Detail::StepSync::EveryStep);
                }

                /**
                 * @brief Starts one step on the worker pool and returns without waiting for it.
                 * @return The handle of the step, or the Error that kept it from starting.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Result<RunHandle<Owner>, Error> run_async() noexcept
                {
                    if (std::optional<Error> refusal = entry_refusal())
                        return make_error(std::move(*refusal));

                    Detail::AsyncRunState state;
                    if (!Detail::supports_asynchronous_run(owner().run_context()))
                        return make_error(Detail::error_without_throwing(
                            ErrorCode::Unsupported,
                            "run_async(): the active execution engine cannot start a run without "
                            "blocking; call run()."));

                    Result<void> built = build_plan_guarded(state.compile_ms);
                    if (!built)
                        return make_error(std::move(built.error()));

                    Result<void> submitted;
                    Result<void> thrown = guarded(
                        ErrorCode::KernelFailed, [this, &state, &submitted]()
                    {
                        submitted = submit_async_step(state);
                    });
                    Result<void> started = first_failure(std::move(thrown), std::move(submitted));
                    if (!started)
                        return make_error(std::move(started.error()));

                    return RunHandle<Owner>(&owner(), async_record_, state);
                }

                /**
                 * @brief Adds a hook this graph calls on the running thread before each step.
                 * @param hook Callable invoked as hook(std::size_t finished_steps) with the
                 *        steps the run in progress replayed before this one.
                 * @return The graph, so that calls chain.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class Hook>
                Owner& before_each_step(Hook&& hook)
                {
                    if constexpr (Detail::accepts_step_hook<Hook>())
                        step_hooks_.add(std::forward<Hook>(hook));
                    return owner();
                }

                /**
                 * @brief Asks this graph's run in flight to stop, from any thread.
                 *
                 * It does nothing when this graph has no run in flight, so it never stops a run
                 * of another graph on the same runtime.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void cancel() noexcept
                {
                    if (runs_in_flight_.load(std::memory_order_acquire) != 0)
                        Detail::cancel_run(owner().run_context());
                }

                /**
                 * @brief Builds the plan if none is current and runs nothing.
                 * @return Nothing, or the Error run() would return before its first step: the
                 *         recorder's refusal, a refused entry, or why the plan could not be built.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Result<void> compile() noexcept
                {
                    if (std::optional<Error> refusal = entry_refusal())
                        return make_error(std::move(*refusal));

                    double compile_ms = 0.0;
                    return build_plan_guarded(compile_ms);
                }

                /**
                 * @brief Requests or withdraws per-node device timing for this graph.
                 * @return The graph, so that calls chain.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                Owner& set_profiling(bool requested) noexcept
                {
                    owner().request_profiling(requested);
                    return owner();
                }

                /**
                 * @brief Returns whether this graph requests per-node device timing.
                 * @return The last value given to set_profiling(), or RuntimeSettings::profiling
                 *         when it was never called.
                 */
                [[nodiscard]] bool is_profiling_requested() const noexcept
                {
                    return owner().profiling_request();
                }

            protected:
                StepDriver() = default;

                /**
                 * @brief Ends a step still pending on other and takes its step hooks; the
                 *        marker of a step never moves.
                 * @pre No step started with run_async() on other is pending.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                StepDriver(StepDriver&& other) noexcept
                {
                    other.end_step_before_move();
                    step_hooks_ = std::move(other.step_hooks_);
                }

                /**
                 * @brief Ends a step still pending on either side and takes other's step
                 *        hooks; the marker of a step never moves.
                 * @pre No step started with run_async() on either graph is pending.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                StepDriver& operator=(StepDriver&& other) noexcept
                {
                    if (this != &other)
                    {
                        end_step_before_move();
                        other.end_step_before_move();
                        step_hooks_ = std::move(other.step_hooks_);
                    }
                    return *this;
                }

                StepDriver(const StepDriver&) = delete;
                StepDriver& operator=(const StepDriver&) = delete;
                ~StepDriver() = default;

                /**
                 * @brief Waits for a step still pending, advances the States and drops its outcome.
                 * @pre The owner's plan, hooks and context are alive.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void end_pending_step() noexcept
                {
                    if (!async_step_pending())
                        return;

                    close_async_step();
                    const Result<void> dropped = guarded(ErrorCode::KernelFailed, [this]()
                    {
                        // A step the engine failed advances no State.
                        if (!Detail::complete_step(owner().run_context()))
                            return;
                        const Detail::StepMachinery& steps = owner().run_steps();
                        if (!steps.take_fault())
                            steps.after_step();
                    });
                    static_cast<void>(dropped);
                }

            private:
                friend class RunHandle<Owner>;

                /** @brief Counts one run of this graph in flight until destroyed, unless kept. */
                class InFlightMark
                {
                    public:
                        /** @brief Adds one run to @p count. */
                        explicit InFlightMark(std::atomic<std::uint32_t>& count) noexcept
                            : count_(&count)
                        {
                            count.fetch_add(1, std::memory_order_acq_rel);
                        }

                        /** @brief Takes the run back off the count unless keep() was called. */
                        ~InFlightMark()
                        {
                            if (count_ != nullptr)
                                count_->fetch_sub(1, std::memory_order_acq_rel);
                        }

                        InFlightMark(const InFlightMark&) = delete;
                        InFlightMark& operator=(const InFlightMark&) = delete;
                        InFlightMark(InFlightMark&&) = delete;
                        InFlightMark& operator=(InFlightMark&&) = delete;

                        /** @brief Leaves the run counted past this object's life. */
                        void keep() noexcept { count_ = nullptr; }

                    private:
                        /** @brief The count to restore; null once kept. */
                        std::atomic<std::uint32_t>* count_;
                };

                /** @brief Returns the graph this driver runs. */
                Owner& owner() noexcept { return static_cast<Owner&>(*this); }

                /** @brief Returns the graph this driver runs, read-only. */
                const Owner& owner() const noexcept { return static_cast<const Owner&>(*this); }

                /**
                 * @brief Names the plan, the hooks and the checks of the graph for one call into
                 *        the library.
                 * @pre The graph's plan is built.
                 */
                Detail::StepTarget target() noexcept
                {
                    Owner& self = owner();
                    Detail::StepTarget named;
                    named.context = &self.run_context();
                    named.plan = self.run_plan();
                    named.steps = &self.run_steps();
                    named.hooks = &step_hooks_;
                    named.owner = &self;
                    named.rearm = [](void* graph) { static_cast<Owner*>(graph)->rearm_plan(); };
                    named.node_count = self.node_count();
                    named.may_chain = self.may_chain();
                    return named;
                }

                /** @brief Returns the test that continues a run for a fixed number of steps. */
                static auto for_steps(std::size_t steps) noexcept
                {
                    return [steps](std::size_t completed) { return completed < steps; };
                }

                /**
                 * @brief Returns the time point a budget ends at, saturated at the clock's last.
                 * @param now A time point not before the clock's epoch.
                 * @return now for a budget of zero or less, and time_point::max() for a budget
                 *         that ends within a second of it or past it.
                 */
                template <class Rep, class Period>
                static std::chrono::steady_clock::time_point
                deadline_after(std::chrono::steady_clock::time_point now,
                               std::chrono::duration<Rep, Period> budget) noexcept
                {
                    using clock = std::chrono::steady_clock;
                    using Seconds = std::chrono::duration<long double>;

                    if (budget <= std::chrono::duration<Rep, Period>::zero())
                        return now;
                    const clock::duration room = clock::time_point::max() - now;
                    if (Seconds(budget) >= Seconds(room) - Seconds(1))
                        return clock::time_point::max();
                    return now + std::chrono::duration_cast<clock::duration>(budget);
                }

                /** @brief Returns the test that continues a run until budget has elapsed. */
                template <class Rep, class Period>
                static auto within(std::chrono::duration<Rep, Period> budget) noexcept
                {
                    const auto deadline = deadline_after(std::chrono::steady_clock::now(), budget);
                    return [deadline](std::size_t completed)
                    {
                        return completed == 0 || std::chrono::steady_clock::now() < deadline;
                    };
                }

                /** @brief Returns the test that continues a run until done returns true. */
                template <class Predicate>
                static auto until(Predicate done)
                {
                    return [done = std::move(done)](std::size_t) mutable { return !done(); };
                }

                /** @brief Refuses, at compile time, a stop test whose move may throw. */
                template <class Predicate>
                static constexpr void require_nothrow_move() noexcept
                {
                    static_assert(std::is_nothrow_move_constructible<Predicate>::value,
                                  "run_until() is noexcept and moves its stop test, so the "
                                  "stop test must be nothrow move constructible; pass a lambda "
                                  "that captures by reference.");
                }

                /**
                 * @brief Runs work and returns what it throws as an Error.
                 * @param fallback Code of a std::exception: CompileFailed while the plan is
                 *        built, KernelFailed once a step may have started.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class Work>
                [[nodiscard]] static Result<void> guarded(ErrorCode fallback, Work&& work) noexcept
                {
                    return Detail::translate_exceptions(fallback, std::forward<Work>(work));
                }

                /**
                 * @brief Returns the failure of two outcomes of one call, the thrown one first.
                 * @param thrown What guarded() returned for the call.
                 * @param returned What the call itself returned.
                 */
                [[nodiscard]] static Result<void> first_failure(Result<void> thrown,
                                                                Result<void> returned) noexcept
                {
                    return thrown ? std::move(returned) : std::move(thrown);
                }

                /**
                 * @brief Returns why no run may start now, or nothing when one may.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] std::optional<Error> entry_refusal() noexcept
                {
                    Owner& self = owner();
                    // A refused recording outlives the check for work: it may be the only rule.
                    if (const Error* refused = self.recording_refusal())
                        return Detail::error_without_throwing(*refused);
                    if (!self.has_work())
                        return Detail::error_without_throwing(ErrorCode::InvalidGraph,
                                                              Owner::K_NOTHING_TO_RUN);
                    if (async_step_pending())
                        return Detail::error_without_throwing(ErrorCode::InvalidState,
                            "run() and run_async() are refused while a step started with "
                            "run_async() is pending on this graph; call wait() on its RunHandle "
                            "first.");
                    if (Detail::run_in_progress(self.run_context()))
                        return Detail::error_without_throwing(ErrorCode::InvalidState,
                            "run() and run_async() are refused while another "
                            "graph's run is in flight on this runtime, and from inside a task of "
                            "a run; settle that run first: let its blocking call return, or call "
                            "wait() on its RunHandle.");
                    if (self.has_dangling_resource())
                        return Detail::error_without_throwing(ErrorCode::DanglingResource,
                                                              Owner::K_DANGLING_RESOURCE);
                    return std::nullopt;
                }

                /**
                 * @brief Builds the plan if it is stale and prepares the graph for its first step.
                 * @return Milliseconds the build took, zero when the plan was current, or the
                 *         error the owner returned for a plan it could not build.
                 */
                Result<double> build_plan()
                {
                    using clock = std::chrono::steady_clock;
                    Owner& self = owner();
                    double compile_ms = 0.0;
                    if (self.plan_is_stale())
                    {
                        const auto started = clock::now();
                        Result<void> rebuilt = self.rebuild_plan();
                        if (!rebuilt)
                            return make_error(std::move(rebuilt.error()));
                        compile_ms = std::chrono::duration<double, std::milli>(
                            clock::now() - started).count();
                    }
                    Result<void> prepared = self.prepare_run();
                    if (!prepared)
                        return make_error(std::move(prepared.error()));
                    return compile_ms;
                }

                /**
                 * @brief Builds the plan and turns what the build throws or returns into an Error.
                 * @param compile_ms Receives what build_plan() returned; unchanged on a failure.
                 * @return Nothing, or why the graph has no plan to run.
                 */
                [[nodiscard]] Result<void> build_plan_guarded(double& compile_ms) noexcept
                {
                    Result<void> refused;
                    Result<void> thrown =
                        guarded(ErrorCode::CompileFailed, [this, &compile_ms, &refused]()
                    {
                        Result<double> built = build_plan();
                        if (built)
                            compile_ms = *built;
                        else
                            refused = make_error(std::move(built.error()));
                    });
                    return first_failure(std::move(thrown), std::move(refused));
                }

                /**
                 * @brief Submits one step to the pool and marks it pending.
                 * @param state Receives what the settling call needs; compile_ms is kept.
                 * @return Nothing, or why the engine did not take the step; none is pending then.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                Result<void> submit_async_step(Detail::AsyncRunState& state)
                {
                    if (async_record_ == nullptr)
                        async_record_ = std::make_shared<Detail::AsyncStepRecord>();

                    // The run counts as in flight before its cancellation is reset.
                    InFlightMark in_flight(runs_in_flight_);
                    Result<void> submitted = Detail::submit_step(target(), state);
                    if (!submitted)
                        return submitted;

                    in_flight.keep();
                    if (++async_step_serial_ == Detail::AsyncStepRecord::K_NO_STEP)
                        ++async_step_serial_;
                    state.serial = async_step_serial_;
                    async_record_->pending_serial = state.serial;
                    return {};
                }

                /** @brief Returns whether a run_async() step has not been settled yet. */
                bool async_step_pending() const noexcept
                {
                    return async_record_ != nullptr &&
                           async_record_->pending_serial != Detail::AsyncStepRecord::K_NO_STEP;
                }

                /**
                 * @brief Marks the pending step over for its handle, for cancel() and for the
                 *        run entries.
                 * @pre async_step_pending() is true.
                 */
                void close_async_step() noexcept
                {
                    async_record_->pending_serial = Detail::AsyncStepRecord::K_NO_STEP;
                    runs_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                }

                /** @brief Returns whether waiting on the pending step would return at once. */
                bool async_step_finished() { return Detail::step_finished(owner().run_context()); }

                /**
                 * @brief Waits for the pending step and closes it out.
                 * @pre state is that of the step pending here; RunHandle::owns_step() tests it.
                 * @param report The report to fill; the caller has reset it.
                 * @return Nothing, or what ended the step.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                [[nodiscard]] Result<void> settle_async_step(const Detail::AsyncRunState& state,
                                                             RunReport& report) noexcept
                {
                    // No step is pending from here on, whatever the wait below returns.
                    close_async_step();
                    Result<void> finished;
                    Result<void> thrown =
                        guarded(ErrorCode::KernelFailed, [this, &state, &report, &finished]()
                    {
                        finished = Detail::complete_step(owner().run_context());
                        if (finished)
                            finished = Detail::finish_step(target(), state, report);
                    });
                    return first_failure(std::move(thrown), std::move(finished));
                }

                /**
                 * @brief Ends a step still pending when its graph is moved.
                 * @pre No step is pending; SR_ASSERT checks it.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void end_step_before_move() noexcept
                {
                    SR_ASSERT(!async_step_pending(),
                              "A graph was moved while its run_async() step was pending.");
                    end_pending_step();
                }

                /**
                 * @brief Runs a whole run into a report of its own.
                 * @param keep_going Called with the completed step count before each step; true
                 *        continues.
                 * @return The report, or the Error that ended the run.
                 */
                template <class KeepGoing>
                Result<RunReport, Error>
                drive(KeepGoing keep_going, std::size_t expected_invocations,
                      Detail::StepSync sync) noexcept
                {
                    RunReport report;
                    auto driven = drive(std::move(keep_going), report, expected_invocations, sync);
                    if (!driven)
                        return make_error(std::move(driven.error()));
                    return std::move(report);
                }

                /**
                 * @brief Runs a whole run into a caller-owned report; every run entry ends here.
                 * @param keep_going Called with the completed step count before each step; true
                 *        continues.
                 * @param report The report to fill; reset before anything else happens.
                 * @return A pointer to report, or the Error that ended the run.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class KeepGoing>
                Result<RunReport*, Error>
                drive(KeepGoing keep_going, RunReport& report,
                      std::size_t expected_invocations, Detail::StepSync sync) noexcept
                {
                    // Every return below leaves report reset and marked unsuccessful.
                    report.reset();
                    report.is_successful = false;

                    if (std::optional<Error> refusal = entry_refusal())
                        return make_error(std::move(*refusal));

                    double compile_ms = 0.0;
                    Result<void> built = build_plan_guarded(compile_ms);
                    if (!built)
                        return make_error(std::move(built.error()));

                    Result<void> stepped;
                    Result<void> thrown = guarded(ErrorCode::KernelFailed, [&]()
                    {
                        stepped = execute_steps(
                            keep_going, report, compile_ms, expected_invocations, sync);
                    });
                    Result<void> ran = first_failure(std::move(thrown), std::move(stepped));
                    if (!ran)
                        return make_error(std::move(ran.error()));
                    return &report;
                }

                /**
                 * @brief Runs the steps over the built plan and fills the report.
                 * @param compile_ms What build_plan_guarded() stored for this run.
                 * @return Nothing, or the fault a step recorded; the report is not filled then.
                 * @throws Whatever a step throws; drive() maps it.
                 */
                template <class KeepGoing>
                Result<void> execute_steps(KeepGoing& keep_going,
                                           RunReport& report,
                                           double compile_ms, std::size_t expected_invocations,
                                           Detail::StepSync sync)
                {
                    // The run counts as in flight before its cancellation is reset.
                    const InFlightMark in_flight(runs_in_flight_);
                    return Detail::run_steps(target(), Detail::StepPredicate(keep_going), report,
                                             compile_ms, expected_invocations,
                                             sync);
                }

                /**
                 * @brief Names the pending run_async() step for the handles; null until the
                 *        first run_async().
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                std::shared_ptr<Detail::AsyncStepRecord> async_record_;

                /** @brief Numbers the steps run_async() started here; a handle keeps its own. */
                std::uint32_t async_step_serial_ = Detail::AsyncStepRecord::K_NO_STEP;

                /** @brief Counts this graph's blocking runs and pending step; cancel() reads it. */
                std::atomic<std::uint32_t> runs_in_flight_{0};

                /** @brief The hooks before_each_step() added, in the order they were added. */
                Detail::BeforeStepHooks step_hooks_;
        };
    } // namespace Detail
} // namespace SushiRuntime
