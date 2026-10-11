/**************************************************************************/
/* run_handle.hpp                                                         */
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
 * @file run_handle.hpp
 * @brief Declares RunHandle, the handle to a step started asynchronously.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/observability/run_report.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace detail
    {
        /**
         * @brief Holds what an asynchronous step must remember from the moment it started.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct AsyncRunState
        {
            Internal::Execution::ExecutionMetrics before{};      /**< Scheduler counters at submit. */
            std::size_t device_to_host_before = 0;     /**< Transfer counter at submit. */
            std::size_t host_to_device_before = 0;     /**< Transfer counter at submit. */
            std::uint64_t offloaded_before = 0;        /**< Offload counter at submit. */
            std::chrono::steady_clock::time_point started{}; /**< When the roots went in. */
            double compile_ms = 0.0;                   /**< Time spent compiling, if it did. */
            std::size_t task_count = 0;                /**< Tasks this step dispatched. */
            std::uint32_t serial = 0;                  /**< Number the driver gave the step. */
        };

        /**
         * @brief Names the asynchronous step pending on one graph, for the handles it gave.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct AsyncStepRecord
        {
            /** @brief Stands for "no step is pending"; no step is ever given this number. */
            static constexpr std::uint32_t K_NO_STEP = 0;

            std::uint32_t pending_serial = K_NO_STEP;  /**< Number of the pending step. */
        };

        /**
         * @brief Builds an Error without throwing.
         * @param message Text of the Error; null-terminated.
         * @return The Error, with an empty message when the text could not be allocated.
         */
        [[nodiscard]] inline Error error_without_throwing(Errc code,
                                                                const char* message) noexcept
        {
            try
            {
                return Error{code, message};
            }
            catch (...)
            {
                return Error{code, {}};
            }
        }

        /**
         * @brief Copies an Error without throwing.
         * @return The copy, with an empty message when the text could not be allocated.
         */
        [[nodiscard]] inline Error error_without_throwing(
            const Error& source) noexcept
        {
            try
            {
                return source;
            }
            catch (...)
            {
                return Error{source.code, {}};
            }
        }
    } // namespace detail

    template <class Owner>
    class StepDriver;

    /**
     * @brief Owns a step that was started but not yet waited for, and finishes it on
     *        destruction.
     * @tparam Owner The graph type that produced this handle: Graph or DynamicGraph.
     * @pre The graph that produced a pending handle is not moved.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    template <class Owner>
    class [[nodiscard]] RunHandle
    {
        public:
            /** @brief Constructs an empty handle that owns no step. */
            RunHandle() = default;

            /**
             * @brief Takes over other's pending step, leaving it empty.
             * @param other Handle to take the step from; owns nothing afterwards.
             */
            RunHandle(RunHandle&& other) noexcept
                : owner_(other.owner_), record_(std::move(other.record_)), state_(other.state_)
            {
                other.owner_ = nullptr;
            }

            /**
             * @brief Settles this handle's own step, then takes over other's.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            RunHandle& operator=(RunHandle&& other) noexcept
            {
                if (this != &other)
                {
                    settle();
                    owner_ = other.owner_;
                    record_ = std::move(other.record_);
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
             * @brief Returns whether this handle still owns a step that must be waited for.
             * @return True until wait() or try_wait() consumes it, it is moved from, or its
             *         graph ends the step by being moved or destroyed.
             */
            [[nodiscard]] bool pending() const noexcept { return owns_step(); }

            /**
             * @brief Returns whether waiting would return immediately.
             * @return True when the step is done, or the handle owns no step.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] bool ready() const
            {
                return !owns_step() || owner_->async_step_finished();
            }

            /**
             * @brief Blocks until the step finishes, advances every State and returns the
             *        report.
             * @throws SushiException With the error try_wait() would return.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Internal::Observability::RunReport wait()
            {
                auto waited = try_wait();
                if (!waited)
                    throw SushiException(std::move(waited.error()));
                return std::move(*waited);
            }

            /**
             * @brief Blocks until the step finishes and reports into reuse.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @throws SushiException With the error try_wait() would return.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Internal::Observability::RunReport& wait(Internal::Observability::RunReport& reuse)
            {
                auto waited = try_wait(reuse);
                if (!waited)
                    throw SushiException(std::move(waited.error()));
                return reuse;
            }

            /**
             * @brief Blocks until the step finishes and returns the outcome as a value.
             * @return The report, or the Error that ended the step.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<Internal::Observability::RunReport, Error>
            try_wait() noexcept
            {
                Internal::Observability::RunReport report;
                auto waited = try_wait(report);
                if (!waited)
                    return make_error(std::move(waited.error()));
                return std::move(report);
            }

            /**
             * @brief Blocks until the step finishes and reports into reuse, without throwing.
             * @param reuse The report to overwrite; its previous contents are lost.
             * @return A pointer to reuse, or the Error that ended the step.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] Result<Internal::Observability::RunReport*, Error>
            try_wait(Internal::Observability::RunReport& reuse) noexcept
            {
                // Every return below leaves reuse reset and marked unsuccessful.
                reuse.reset();
                reuse.is_successful = false;

                if (owner_ == nullptr)
                    return make_error(detail::error_without_throwing(
                        Errc::invalid_state,
                        "RunHandle::wait(): this handle owns no step, because it was "
                        "already waited for or moved from; start a step with run_async()."));

                // owner_ is null from here on, so no later call waits on this step again.
                const bool owned = owns_step();
                Owner* owner = owner_;
                owner_ = nullptr;
                record_.reset();

                if (!owned)
                    return make_error(detail::error_without_throwing(
                        Errc::invalid_state,
                        "RunHandle::wait(): the graph was moved or destroyed while this step "
                        "was pending, which ended the step and dropped its report; wait on "
                        "the handle first."));

                Error failure = owner->settle_async_step(state_, reuse);
                if (failure.code != Errc::ok)
                    return make_error(std::move(failure));
                return &reuse;
            }

        private:
            friend class StepDriver<Owner>;

            RunHandle(Owner* owner, std::shared_ptr<detail::AsyncStepRecord> record,
                      const detail::AsyncRunState& state) noexcept
                : owner_(owner), record_(std::move(record)), state_(state)
            {
            }

            /**
             * @brief Returns whether the step this handle was given is still pending.
             * @return True only while the graph at owner_ is alive and has not ended the step.
             */
            [[nodiscard]] bool owns_step() const noexcept
            {
                return record_ != nullptr && record_->pending_serial == state_.serial;
            }

            /**
             * @brief Finishes a step nobody waited for and drops its outcome.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void settle() noexcept
            {
                if (owner_ == nullptr)
                    return;
                Internal::Observability::RunReport dropped;
                static_cast<void>(try_wait(dropped));
            }

            /** @brief The graph that started the step; dereferenced only while owns_step(). */
            Owner* owner_ = nullptr;

            /** @brief The graph's record of its pending step; outlives the graph. */
            std::shared_ptr<detail::AsyncStepRecord> record_;

            /** @brief What run_async() recorded when it submitted the step. */
            detail::AsyncRunState state_{};
    };



} // namespace SushiRuntime
