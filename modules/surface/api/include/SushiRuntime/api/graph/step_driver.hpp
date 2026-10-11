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
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <type_traits>
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
             * @throws SushiException With the error try_run() would return.
             */
            Internal::Observability::RunReport run() { return unwrap(try_run()); }

            /**
             * @brief Runs the given number of successive steps and blocks until they finish.
             * @return A RunReport aggregating the whole run.
             * @throws SushiException With the error try_run() would return.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Internal::Observability::RunReport run(std::size_t steps) { return unwrap(try_run(steps)); }

            /**
             * @brief Runs the graph once into a report the caller owns.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @throws SushiException With the error try_run() would return.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Internal::Observability::RunReport& run(Internal::Observability::RunReport& reuse)
            {
                return unwrap(try_run(reuse));
            }

            /**
             * @brief Runs the given number of steps into a report the caller owns.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A reference to reuse.
             * @throws SushiException With the error try_run() would return.
             */
            Internal::Observability::RunReport& run(std::size_t steps, Internal::Observability::RunReport& reuse)
            {
                return unwrap(try_run(steps, reuse));
            }

            /**
             * @brief Runs steps until a wall-clock budget elapses.
             * @param budget Wall-clock limit; at least one step always runs.
             * @return A RunReport aggregating the steps that ran.
             * @throws SushiException With the error try_run_for() would return.
             */
            template <class Rep, class Period>
            Internal::Observability::RunReport run_for(std::chrono::duration<Rep, Period> budget)
            {
                return unwrap(try_run_for(budget));
            }

            /**
             * @brief Runs steps until a wall-clock budget elapses, into a report the caller
             *        owns.
             * @param budget Wall-clock limit; at least one step always runs.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @throws SushiException With the error try_run_for() would return.
             */
            template <class Rep, class Period>
            Internal::Observability::RunReport& run_for(std::chrono::duration<Rep, Period> budget,
                                              Internal::Observability::RunReport& reuse)
            {
                return unwrap(try_run_for(budget, reuse));
            }

            /**
             * @brief Runs steps until done returns true.
             * @param done Stop test called before each step; the run ends the first time it
             *        returns true.
             * @return A RunReport aggregating the steps that ran.
             * @throws SushiException With the error try_run_until() would return.
             */
            template <class Predicate>
            Internal::Observability::RunReport run_until(Predicate done)
            {
                return unwrap(try_run_until(std::move(done)));
            }

            /**
             * @brief Runs steps until done returns true, into a report the caller owns.
             * @param done Stop test called before each step; the run ends the first time it
             *        returns true.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @throws SushiException With the error try_run_until() would return.
             */
            template <class Predicate>
            Internal::Observability::RunReport& run_until(Predicate done, Internal::Observability::RunReport& reuse)
            {
                return unwrap(try_run_until(std::move(done), reuse));
            }

            /**
             * @brief Runs the graph once and returns the outcome as a value.
             * @return The RunReport, or the Error that ended the run; a failed run of one
             *         step has advanced no State.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<Internal::Observability::RunReport, Error> try_run() noexcept
            {
                return try_run(std::size_t{1});
            }

            /**
             * @brief Runs the given number of steps and returns the outcome as a value.
             * @return The RunReport, or the Error that ended the run. A failed run has
             *         advanced the States once per step before the failing one; on the chain
             *         a device fault of any step shows at the end, with every step but the
             *         last advanced.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<Internal::Observability::RunReport, Error>
            try_run(std::size_t steps) noexcept
            {
                return drive(for_steps(steps), steps, StepSync::AtEnd);
            }

            /**
             * @brief Runs the graph once into a report the caller owns, without throwing.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A pointer to reuse, or the Error that ended the run.
             */
            [[nodiscard]] Result<Internal::Observability::RunReport*, Error>
            try_run(Internal::Observability::RunReport& reuse) noexcept
            {
                return try_run(std::size_t{1}, reuse);
            }

            /**
             * @brief Runs the given number of steps into a report the caller owns, without
             *        throwing.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A pointer to reuse, or the Error that ended the run.
             */
            [[nodiscard]] Result<Internal::Observability::RunReport*, Error>
            try_run(std::size_t steps, Internal::Observability::RunReport& reuse) noexcept
            {
                return drive(for_steps(steps), reuse, steps, StepSync::AtEnd);
            }

            /**
             * @brief Runs steps until a wall-clock budget elapses, without throwing.
             * @param budget Wall-clock limit; at least one step always runs.
             * @return The RunReport, or the Error that ended the run.
             */
            template <class Rep, class Period>
            [[nodiscard]] Result<Internal::Observability::RunReport, Error>
            try_run_for(std::chrono::duration<Rep, Period> budget) noexcept
            {
                return drive(within(budget), 1, StepSync::EveryStep);
            }

            /**
             * @brief Runs steps until a wall-clock budget elapses, into a report the caller
             *        owns, without throwing.
             * @param budget Wall-clock limit; at least one step always runs.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A pointer to reuse, or the Error that ended the run.
             */
            template <class Rep, class Period>
            [[nodiscard]] Result<Internal::Observability::RunReport*, Error>
            try_run_for(std::chrono::duration<Rep, Period> budget,
                        Internal::Observability::RunReport& reuse) noexcept
            {
                return drive(within(budget), reuse, 1, StepSync::EveryStep);
            }

            /**
             * @brief Runs steps until done returns true, without throwing.
             * @tparam Predicate Move-constructible without throwing.
             * @param done Stop test called before each step; an exception it throws ends the
             *        run as kernel_failed.
             * @return The RunReport, or the Error that ended the run.
             */
            template <class Predicate>
            [[nodiscard]] Result<Internal::Observability::RunReport, Error>
            try_run_until(Predicate done) noexcept
            {
                require_nothrow_move<Predicate>();
                return drive(until(std::move(done)), 1, StepSync::EveryStep);
            }

            /**
             * @brief Runs steps until done returns true into the caller's report; never throws.
             * @tparam Predicate Move-constructible without throwing.
             * @param done Stop test called before each step; an exception it throws ends the
             *        run as kernel_failed.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A pointer to reuse, or the Error that ended the run.
             */
            template <class Predicate>
            [[nodiscard]] Result<Internal::Observability::RunReport*, Error>
            try_run_until(Predicate done, Internal::Observability::RunReport& reuse) noexcept
            {
                require_nothrow_move<Predicate>();
                return drive(until(std::move(done)), reuse, 1, StepSync::EveryStep);
            }

            /**
             * @brief Starts one step on the worker pool and returns without waiting for it.
             * @throws SushiException With the error try_run_async() would return.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] RunHandle<Owner> run_async()
            {
                auto started = try_run_async();
                if (!started)
                    throw SushiException(std::move(started.error()));
                return std::move(*started);
            }

            /**
             * @brief Starts one step on the worker pool and returns the handle as a value.
             * @return The handle of the step, or the Error that kept it from starting.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<RunHandle<Owner>, Error> try_run_async() noexcept
            {
                Error failure = entry_refusal();
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));

                detail::AsyncRunState state;
                failure = guarded(Errc::compile_failed, [this, &state]()
                {
                    require_asynchronous_engine();
                    state.compile_ms = build_plan();
                });
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));

                failure = guarded(Errc::kernel_failed, [this, &state]()
                {
                    submit_async_step(state);
                });
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));

                return RunHandle<Owner>(&owner(), async_record_, state);
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
                    owner().run_context().get_execution_engine().cancel();
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
             * @brief Ends a step still pending on other; the marker of a step never moves.
             * @pre No step started with run_async() on other is pending.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            StepDriver(StepDriver&& other) noexcept
            {
                other.end_step_before_move();
            }

            /**
             * @brief Ends a step still pending on either side; the marker of a step never
             *        moves.
             * @pre No step started with run_async() on either graph is pending.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            StepDriver& operator=(StepDriver&& other) noexcept
            {
                if (this != &other)
                {
                    end_step_before_move();
                    other.end_step_before_move();
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
                const Error dropped = guarded(Errc::kernel_failed, [this]()
                {
                    async_engine().complete();
                    const detail::StepMachinery& steps = owner().run_steps();
                    steps.check_faults();
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

            /** @brief Says whether a chained run may skip the host wait between steps. */
            enum class StepSync
            {
                AtEnd,     /**< Fixed step count: submit back to back, one wait at the end. */
                EveryStep  /**< The stop test may read results: wait after every step. */
            };

            /** @brief Returns the graph this driver runs. */
            Owner& owner() noexcept { return static_cast<Owner&>(*this); }

            /** @brief Returns the graph this driver runs, read-only. */
            const Owner& owner() const noexcept { return static_cast<const Owner&>(*this); }

            /** @brief Returns the engine the graph's steps run on. */
            Internal::Execution::IExecutionEngine& async_engine()
            {
                return owner().run_context().get_execution_engine();
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
                              "try_run_until() is noexcept and moves its stop test, so the "
                              "stop test must be nothrow move constructible; pass a lambda "
                              "that captures by reference.");
            }

            /** @brief Throws the carried error, or returns the report. */
            static Internal::Observability::RunReport
            unwrap(Result<Internal::Observability::RunReport, Error> driven)
            {
                if (!driven)
                    throw SushiException(std::move(driven.error()));
                return std::move(*driven);
            }

            /** @brief Throws the carried error, or returns the report the caller owns. */
            static Internal::Observability::RunReport&
            unwrap(Result<Internal::Observability::RunReport*, Error> driven)
            {
                if (!driven)
                    throw SushiException(std::move(driven.error()));
                return **driven;
            }

            /**
             * @brief Runs work and turns anything it throws into an Error.
             * @param fallback Code of an exception that carries none: compile_failed while
             *        the plan is built, kernel_failed once a step may have started.
             * @return An Error whose code is ok when work returned; its message is empty when
             *         the text could not be allocated.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            template <class Work>
            static Error guarded(Errc fallback, Work&& work) noexcept
            {
                try
                {
                    work();
                    return Error{};
                }
                catch (const SushiException& e)
                {
                    return detail::error_without_throwing(e.error());
                }
                catch (const sycl::exception& e)
                {
                    return detail::error_without_throwing(Errc::kernel_failed, e.what());
                }
                catch (const std::exception& e)
                {
                    return detail::error_without_throwing(fallback, e.what());
                }
                catch (...)
                {
                    return detail::error_without_throwing(fallback,
                        "An object that does not derive from std::exception was thrown.");
                }
            }

            /**
             * @brief Returns why no run may start now, or an Error whose code is ok.
             *
             * It reads the engine's run marker once, so a run entry costs one atomic load
             * here whatever its step count.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Error entry_refusal() noexcept
            {
                Owner& self = owner();
                if (!self.has_work())
                    return detail::error_without_throwing(Errc::invalid_graph,
                                                          Owner::K_NOTHING_TO_RUN);
                if (async_step_pending())
                    return detail::error_without_throwing(Errc::invalid_state,
                        "run(), run_async() and their try_ forms are refused while a step "
                        "started with run_async() is pending on this graph; call wait() or "
                        "try_wait() on its RunHandle first.");
                if (self.run_context().get_execution_engine().run_in_progress())
                    return detail::error_without_throwing(Errc::invalid_state,
                        "run(), run_async() and their try_ forms are refused while another "
                        "graph's run is in flight on this runtime, and from inside a task of "
                        "a run; settle that run first: let its blocking call return, or call "
                        "wait() or try_wait() on its RunHandle.");
                if (self.has_dangling_resource())
                    return detail::error_without_throwing(Errc::dangling_resource,
                                                          Owner::K_DANGLING_RESOURCE);
                return Error{};
            }

            /** @brief Throws unsupported when the engine cannot start a run without blocking. */
            void require_asynchronous_engine()
            {
                if (!async_engine().supports_asynchronous_run())
                    throw SushiException(Error{Errc::unsupported,
                        "run_async(): the active execution engine cannot start a run "
                        "without blocking; call run()."});
            }

            /**
             * @brief Builds the plan if it is stale and prepares the graph for its first step.
             * @return Milliseconds the build took; zero when the plan was current.
             */
            double build_plan()
            {
                using clock = std::chrono::steady_clock;
                Owner& self = owner();
                double compile_ms = 0.0;
                if (self.plan_is_stale())
                {
                    const auto started = clock::now();
                    self.rebuild_plan();
                    compile_ms = std::chrono::duration<double, std::milli>(
                        clock::now() - started).count();
                }
                self.prepare_run();
                return compile_ms;
            }

            /**
             * @brief Submits one step to the pool and marks it pending.
             * @param state Receives what the settling call needs; compile_ms is kept.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void submit_async_step(detail::AsyncRunState& state)
            {
                Owner& self = owner();
                Internal::Execution::RuntimeContext& context = self.run_context();
                auto& engine = context.get_execution_engine();
                ::SushiRuntime::Internal::Graph::CompiledGraph& plan = self.run_plan();
                const detail::StepMachinery& steps = self.run_steps();

                if (async_record_ == nullptr)
                    async_record_ = std::make_shared<detail::AsyncStepRecord>();

                // The run counts as in flight before its cancellation is reset.
                InFlightMark in_flight(runs_in_flight_);
                steps.resync();
                engine.reset_cancellation();
                if (plan.profiling)
                    engine.reset_profiling(plan);

                state.before = engine.metrics();
                const Internal::Observability::TransferCounters& transfers = context.transfers();
                state.device_to_host_before = transfers.device_to_host();
                state.host_to_device_before = transfers.host_to_device();
                state.offloaded_before = detail::offloaded_dispatches(context);
                state.task_count = self.node_count();

                steps.before_step();
                self.rearm_plan();
                state.started = std::chrono::steady_clock::now();
                engine.submit(plan);

                in_flight.keep();
                if (++async_step_serial_ == detail::AsyncStepRecord::K_NO_STEP)
                    ++async_step_serial_;
                state.serial = async_step_serial_;
                async_record_->pending_serial = state.serial;
            }

            /** @brief Returns whether a run_async() step has not been settled yet. */
            bool async_step_pending() const noexcept
            {
                return async_record_ != nullptr &&
                       async_record_->pending_serial != detail::AsyncStepRecord::K_NO_STEP;
            }

            /**
             * @brief Marks the pending step over for its handle, for cancel() and for the
             *        run entries.
             * @pre async_step_pending() is true.
             */
            void close_async_step() noexcept
            {
                async_record_->pending_serial = detail::AsyncStepRecord::K_NO_STEP;
                runs_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            }

            /** @brief Returns whether waiting on the pending step would return at once. */
            bool async_step_finished() { return async_engine().run_finished(); }

            /**
             * @brief Waits for the pending step and closes it out.
             * @pre state is that of the step pending here; RunHandle::owns_step() tests it.
             * @param report The report to fill; the caller has reset it.
             * @return An Error whose code is ok, or what ended the step.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Error settle_async_step(const detail::AsyncRunState& state,
                                          Internal::Observability::RunReport& report) noexcept
            {
                // No step is pending from here on, whatever the wait below returns.
                close_async_step();
                return guarded(Errc::kernel_failed, [this, &state, &report]()
                {
                    async_engine().complete();
                    finish_async_step(state, report);
                });
            }

            /**
             * @brief Closes out an asynchronous step once the engine reports it done.
             * @param report The report to fill; reset before it is written.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void finish_async_step(const detail::AsyncRunState& state,
                                   Internal::Observability::RunReport& report)
            {
                const auto finished = std::chrono::steady_clock::now();
                Owner& self = owner();
                Internal::Execution::RuntimeContext& context = self.run_context();
                auto& engine = context.get_execution_engine();
                const detail::StepMachinery& steps = self.run_steps();
                ::SushiRuntime::Internal::Graph::CompiledGraph& plan = self.run_plan();

                steps.check_faults();
                steps.after_step();

                const auto after = engine.metrics();
                detail::fill_run_report(
                    report, context, state.before, after, state.offloaded_before,
                    state.task_count, state.compile_ms,
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
            }

            /**
             * @brief Ends a step still pending when its graph is moved.
             * @pre No step is pending; a debug build asserts it.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void end_step_before_move() noexcept
            {
                assert(!async_step_pending() &&
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
            Result<Internal::Observability::RunReport, Error>
            drive(KeepGoing keep_going, std::size_t expected_invocations,
                  StepSync sync) noexcept
            {
                Internal::Observability::RunReport report;
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
            Result<Internal::Observability::RunReport*, Error>
            drive(KeepGoing keep_going, Internal::Observability::RunReport& report,
                  std::size_t expected_invocations, StepSync sync) noexcept
            {
                // Every return below leaves report reset and marked unsuccessful.
                report.reset();
                report.is_successful = false;

                Error failure = entry_refusal();
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));

                double compile_ms = 0.0;
                failure = guarded(Errc::compile_failed, [this, &compile_ms]()
                {
                    compile_ms = build_plan();
                });
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));

                failure = guarded(Errc::kernel_failed, [&]()
                {
                    execute_steps(keep_going, report, compile_ms, expected_invocations, sync);
                });
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));
                return &report;
            }

            /**
             * @brief Runs the steps over the built plan and fills the report.
             * @param compile_ms What build_plan() returned for this run.
             * @throws Whatever a step throws; drive() maps it.
             */
            template <class KeepGoing>
            void execute_steps(KeepGoing& keep_going, Internal::Observability::RunReport& report,
                               double compile_ms, std::size_t expected_invocations,
                               StepSync sync)
            {
                using clock = std::chrono::steady_clock;

                Owner& self = owner();
                ::SushiRuntime::Internal::Graph::CompiledGraph& plan = self.run_plan();
                const detail::StepMachinery& steps = self.run_steps();

                // State views point at their current and next buffers before the first step.
                steps.resync();

                Internal::Execution::RuntimeContext& context = self.run_context();
                auto& engine = context.get_execution_engine();

                // The run counts as in flight before its cancellation is reset.
                const InFlightMark in_flight(runs_in_flight_);
                engine.reset_cancellation();

                // Profiling accumulators are cleared only when the plan requests profiling.
                const bool profiling = plan.profiling;
                if (profiling)
                    engine.reset_profiling(plan, expected_invocations);

                const auto before = engine.metrics();
                const Internal::Observability::TransferCounters& transfers = context.transfers();
                const std::size_t d2h_before = transfers.device_to_host();
                const std::size_t h2d_before = transfers.host_to_device();
                const std::uint64_t offloaded_before = detail::offloaded_dispatches(context);
                const auto t0 = clock::now();

                const bool chained = self.runs_chained();
                const std::size_t executed = chained
                    ? run_chained(keep_going, engine, sync)
                    : run_scheduled(keep_going, engine);

                const auto t1 = clock::now();
                const auto after = engine.metrics();

                const std::size_t total_tasks = self.node_count() * executed;
                detail::fill_run_report(
                    report, context, before, after, offloaded_before, total_tasks,
                    compile_ms, std::chrono::duration<double, std::milli>(t1 - t0).count());
                report.cancelled = engine.is_cancelled();
                report.layer = chained ? Internal::Observability::ExecutionLayer::Chain
                                       : Internal::Observability::ExecutionLayer::Scheduler;

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
                                      Internal::Execution::IExecutionEngine& engine)
            {
                Owner& self = owner();
                ::SushiRuntime::Internal::Graph::CompiledGraph& plan = self.run_plan();
                const detail::StepMachinery& steps = self.run_steps();
                std::size_t executed = 0;
                while (keep_going(executed) && !engine.is_cancelled())
                {
                    // Live IO and late-bound controls are refreshed before the plan replays.
                    steps.before_step();

                    self.rearm_plan();
                    engine.execute(plan);
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
            std::size_t run_chained(KeepGoing& keep_going, Internal::Execution::IExecutionEngine& engine,
                                    StepSync sync)
            {
                Owner& self = owner();
                ::SushiRuntime::Internal::Graph::CompiledGraph& plan = self.run_plan();
                const detail::StepMachinery& steps = self.run_steps();
                Internal::Execution::ChainRun chain(self.run_context(), plan);
                const bool advance_unchecked = sync == StepSync::AtEnd;
                std::size_t executed = 0;
                while (keep_going(executed) && !engine.is_cancelled())
                {
                    // Under AtEnd the step queued before this one has not advanced the States.
                    if (advance_unchecked && executed != 0)
                        steps.after_step();

                    steps.before_step();
                    self.rearm_plan();
                    chain.submit_step(static_cast<std::uint32_t>(executed));
                    if (sync == StepSync::EveryStep)
                    {
                        chain.wait();
                        steps.check_faults();
                        steps.after_step();
                    }
                    ++executed;
                }
                chain.finish();
                steps.check_faults();

                // Under AtEnd the last step advances the States only once it is checked.
                if (advance_unchecked && executed != 0)
                    steps.after_step();
                return executed;
            }

            /**
             * @brief Names the pending run_async() step for the handles; null until the
             *        first run_async().
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            std::shared_ptr<detail::AsyncStepRecord> async_record_;

            /** @brief Numbers the steps run_async() started here; a handle keeps its own. */
            std::uint32_t async_step_serial_ = detail::AsyncStepRecord::K_NO_STEP;

            /** @brief Counts this graph's blocking runs and pending step; cancel() reads it. */
            std::atomic<std::uint32_t> runs_in_flight_{0};
    };
} // namespace SushiRuntime
