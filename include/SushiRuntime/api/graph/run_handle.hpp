/****************************************************************/
/* run_handle.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file run_handle.hpp
 * @brief Declares RunHandle, the handle to a step started asynchronously.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/graph/README.md
 */

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
             * @brief Holds what an asynchronous step must remember from the moment it started.
             * @see include/SushiRuntime/api/graph/README.md
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
         * @brief Owns a step that was started but not yet waited for, and finishes it on
         *        destruction.
         * @tparam Owner The graph type that produced this handle: Graph or DynamicGraph.
         * @see include/SushiRuntime/api/graph/README.md
         */
        template <class Owner>
        class RunHandle
        {
            public:
                /** @brief Constructs an empty handle that owns no step. */
                RunHandle() = default;

                /**
                 * @brief Takes over @p other's pending step, leaving it empty.
                 * @param other Handle to take the step from; owns nothing afterwards.
                 */
                RunHandle(RunHandle&& other) noexcept
                    : owner_(other.owner_), state_(other.state_)
                {
                    other.owner_ = nullptr;
                }

                /**
                 * @brief Settles this handle's own step, then takes over other's.
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Returns whether waiting would return immediately.
                 * @return True when the step is done, or the handle owns no step.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                bool ready() const
                {
                    return owner_ == nullptr || owner_->async_engine().run_finished();
                }

                /**
                 * @brief Blocks until the step finishes, advances every State and returns the
                 *        report.
                 * @throws Core::SushiException If the handle owns no step.
                 * @throws The first exception a task threw during the step.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport wait()
                {
                    Core::RunReport report;
                    wait(report);
                    return report;
                }

                /**
                 * @brief Blocks until the step finishes and reports into reuse.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @throws Core::SushiException If the handle owns no step.
                 * @throws The first exception a task threw during the step.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Core::RunReport& wait(Core::RunReport& reuse)
                {
                    if (owner_ == nullptr)
                        throw Core::SushiException(Core::Error{
                            Core::Errc::invalid_graph,
                            "RunHandle::wait(): this handle owns no step — it was "
                            "already waited for, or moved from."});

                    // The handle is consumed before the blocking call, even when a task throws.
                    Owner* owner = owner_;
                    owner_ = nullptr;

                    // Cleared before the blocking call, so a failed step shows no old numbers.
                    reuse.reset();
                    reuse.is_successful = false;

                    try
                    {
                        owner->async_engine().complete();
                    }
                    catch (...)
                    {
                        // The owner's in-flight marker is released before the exception propagates.
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

                /**
                 * @brief Finishes a step nobody waited for and drops its outcome without throwing.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
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
