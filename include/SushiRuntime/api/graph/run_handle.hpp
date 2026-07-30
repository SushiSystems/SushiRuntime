/**************************************************************************/
/* run_handle.hpp                                                         */
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

#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/run_report.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief What an asynchronous step must remember from the moment it started.
             *
             * A synchronous run reads its "before" counters and its start time as
             * locals, because the whole run happens inside one function. An
             * asynchronous run does not: the step is submitted in one call and
             * settled in another, so the same values have to travel in the handle.
             * Everything here is a plain value — the handle owns no resources and
             * stays cheap to move.
             */
            struct AsyncRunState
            {
                Execution::ExecutionMetrics before{};      /**< Scheduler counters at submit. */
                std::size_t device_to_host_before = 0;     /**< Transfer counter at submit. */
                std::size_t host_to_device_before = 0;     /**< Transfer counter at submit. */
                std::chrono::steady_clock::time_point started{}; /**< When the roots went in. */
                double compile_ms = 0.0;                   /**< Time spent compiling, if it did. */
                std::size_t task_count = 0;                /**< Tasks this step dispatched. */
            };
        } // namespace detail

        /**
         * @brief A step that was started but not yet waited for.
         *
         * `run()` blocks for the whole step, which is correct for a fixed-step
         * simulation thread but costs **overlap**: the next tick cannot begin while
         * the previous one's state is being extracted for a renderer or an audio
         * system, even though that extract needs no worker threads. `run_async()`
         * returns one of these instead — the work is already proceeding on the
         * worker pool — so the caller can do that extract and then settle up:
         *
         * ```cpp
         * auto step = simulation.run_async();
         * extract_previous_frame();          // overlaps with the step
         * Core::RunReport report = step.wait();
         * ```
         *
         * The handle is move-only and owns the obligation to finish the step: its
         * destructor waits if `wait()` was not called, because the compiled plan
         * must not be destroyed while workers are still touching its nodes. That
         * makes the safe usage the natural one — declare the handle after the graph
         * and ordinary scoping does the rest — but it also means a discarded handle
         * discards the step's outcome, **including a task exception**. Call `wait()`
         * (or `try_wait()`) whenever the result matters, which is nearly always.
         *
         * Exactly one step may be in flight per runtime; starting another while a
         * handle is pending is rejected rather than silently interleaved.
         *
         * @tparam Owner The graph type that produced this handle (Graph or
         *         DynamicGraph). A template rather than an interface so neither
         *         graph type has to grow a vtable for a feature used per step.
         */
        template <class Owner>
        class RunHandle
        {
            public:
                /** @brief Constructs an empty handle that owns no step. */
                RunHandle() = default;

                /** @brief Takes over @p other's pending step, leaving it empty. */
                RunHandle(RunHandle&& other) noexcept
                    : owner_(other.owner_), state_(other.state_)
                {
                    other.owner_ = nullptr;
                }

                /**
                 * @brief Settles this handle's own step, then takes over @p other's.
                 *
                 * Assigning over a pending handle would otherwise abandon a running
                 * step with nobody left to wait for it.
                 */
                RunHandle& operator=(RunHandle&& other) noexcept
                {
                    if (this != &other)
                    {
                        settle();
                        owner_ = other.owner_;
                        state_ = other.state_;
                        other.owner_ = nullptr;
                    }
                    return *this;
                }

                RunHandle(const RunHandle&) = delete;
                RunHandle& operator=(const RunHandle&) = delete;

                /** @brief Waits for an unwaited step, discarding its outcome. */
                ~RunHandle() { settle(); }

                /**
                 * @brief Whether this handle still owns a step that must be waited for.
                 * @return True until wait()/try_wait() consumes it, or it is moved from.
                 */
                bool pending() const noexcept { return owner_ != nullptr; }

                /**
                 * @brief Whether waiting would return immediately.
                 *
                 * Answers "has the graph finished", so a frame loop can keep
                 * overlapping work instead of blocking early. It is not a
                 * substitute for wait(): the pool still has bookkeeping to drain
                 * and the step's state advance has not happened yet.
                 *
                 * @return True when the step is done, or the handle owns no step.
                 */
                bool ready() const
                {
                    return owner_ == nullptr || owner_->async_engine().run_finished();
                }

                /**
                 * @brief Blocks until the step finishes and returns its report.
                 *
                 * Completes the step's lifecycle — every State advances to the field
                 * it just wrote — and consumes the handle, so a second call throws
                 * rather than waiting on a step that is no longer running. A task
                 * exception propagates here, and the state advance is skipped in
                 * that case, exactly as a failed synchronous run leaves its states
                 * untouched.
                 *
                 * @return The report for this step.
                 * @throws Core::SushiException If the handle owns no step.
                 * @throws The first exception a task threw during the step.
                 */
                Core::RunReport wait()
                {
                    Core::RunReport report;
                    wait(report);
                    return report;
                }

                /**
                 * @brief Blocks until the step finishes, reporting into @p reuse.
                 *
                 * The allocation-free counterpart of wait(), for the same reason
                 * Graph::run(RunReport&) exists: an overlapped frame loop settles a
                 * step every tick, and a report it already owns costs nothing to
                 * refill. The report is reset before it is written, so a stale field
                 * from the previous step can never survive into this one.
                 *
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse.
                 * @throws Core::SushiException If the handle owns no step.
                 * @throws The first exception a task threw during the step.
                 */
                Core::RunReport& wait(Core::RunReport& reuse)
                {
                    if (owner_ == nullptr)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_graph,
                            "RunHandle::wait(): this handle owns no step — it was "
                            "already waited for, or moved from."});

                    // Consume the handle BEFORE the blocking call: if a task threw,
                    // the step is still over, and a destructor that tried to wait
                    // again would block forever on a latch nobody will signal.
                    Owner* owner = owner_;
                    owner_ = nullptr;

                    // Clear before the blocking call, so a step that ends in a task
                    // exception cannot leave the previous step's numbers readable in
                    // a report the caller reuses. finish_async_step sets the flag
                    // back on success.
                    reuse.reset();
                    reuse.is_successful = false;

                    try
                    {
                        owner->async_engine().complete();
                    }
                    catch (...)
                    {
                        // The step is over either way. Release the owner's in-flight
                        // marker before propagating, or one throwing kernel would
                        // leave the graph refusing every later run — the failure of
                        // a step must not become the failure of the graph.
                        owner->discard_async_step();
                        throw;
                    }
                    owner->finish_async_step(state_, reuse);
                    return reuse;
                }

                /**
                 * @brief Non-throwing wait.
                 *
                 * The counterpart to try_run() for the asynchronous path: the same
                 * failures, reported as a value.
                 *
                 * @return The report on success, or the error that ended the step.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error> try_wait() noexcept
                {
                    Core::RunReport report;
                    auto waited = try_wait(report);
                    if (!waited)
                        return Core::make_error(std::move(waited.error()));
                    return std::move(report);
                }

                /**
                 * @brief Non-throwing wait into a report the caller owns.
                 *
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or the error that ended
                 *         the step.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_wait(Core::RunReport& reuse) noexcept
                {
                    try
                    {
                        wait(reuse);
                        return &reuse;
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
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (...)
                    {
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed,
                                                            "unknown exception"});
                    }
                }

            private:
                friend Owner;

                RunHandle(Owner* owner, const detail::AsyncRunState& state) noexcept
                    : owner_(owner), state_(state)
                {
                }

                // Finishes a step nobody waited for. A destructor may not throw, so
                // the outcome — including a task exception — is dropped here; that
                // is the documented cost of discarding a handle, and it is still far
                // better than returning while workers hold pointers into the plan.
                void settle() noexcept
                {
                    if (owner_ == nullptr)
                        return;
                    try
                    {
                        wait();
                    }
                    catch (...)
                    {
                        // Deliberately swallowed; see above.
                    }
                }

                Owner* owner_ = nullptr;
                detail::AsyncRunState state_{};
        };

    } // namespace API
} // namespace SushiRuntime
