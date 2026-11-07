/**************************************************************************/
/* host_execution_engine.hpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file host_execution_engine.hpp
 * @brief Declares HostExecutionEngine, which runs a compiled graph on a SeededExecutor with no device and no thread.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Internal::Execution
    {
        /**
         * @brief Runs every node of a compiled graph as one job of a SeededExecutor.
         *
         * A job runs one node and posts the successors it frees, so the seed orders what the
         * dependencies leave unordered. Every call is made on the thread that drives the executor.
         * @see modules/engine/execution/README.md
         */
        class SUSHIRUNTIME_API HostExecutionEngine final : public ExecutionEngine
        {
            public:
                /**
                 * @brief Binds the engine to the executor that runs its nodes and the clock that times them.
                 * @param executor Shared with the caller, who may step it between submit() and complete().
                 * @param clock Shared with the caller, who may advance it inside a node.
                 * @param devices The device count a profiling ledger is sized for; at least 1.
                 * @param notify Told the observation of a run submit_observed() started, at its end.
                 */
                HostExecutionEngine(std::shared_ptr<SeededExecutor> executor,
                                    std::shared_ptr<const Clock> clock, std::size_t devices,
                                    std::function<void(const RunObservation&)> notify);

                HostExecutionEngine(const HostExecutionEngine&) = delete;
                HostExecutionEngine& operator=(const HostExecutionEngine&) = delete;
                HostExecutionEngine(HostExecutionEngine&&) = delete;
                HostExecutionEngine& operator=(HostExecutionEngine&&) = delete;
                ~HostExecutionEngine() override = default;

                /** @brief Runs the graph: submit() then complete(). */
                [[nodiscard]] Result<void> execute(Internal::Graph::CompiledGraph& graph) override;

                /** @brief Returns true: submit() posts the roots and complete() drains the executor. */
                bool supports_asynchronous_run() const override { return true; }

                /**
                 * @brief Posts the graph's roots to the executor and runs nothing yet.
                 * @return InvalidState after shutdown() or during another run, InvalidGraph for a
                 *         graph with no root.
                 */
                [[nodiscard]] Result<void> submit(Internal::Graph::CompiledGraph& graph) override;

                /** @brief Submits as submit() does, and hands the observation over when the run ends. */
                [[nodiscard]] Result<void> submit_observed(Internal::Graph::CompiledGraph& graph) override;

                /**
                 * @brief Runs the executor until the submitted run ends.
                 * @return The first node failure, named, or nothing.
                 */
                [[nodiscard]] Result<void> complete() override;

                /** @brief Reports whether the submitted run's last node has run. */
                bool run_finished() override;

                /** @brief Reports whether a run is between submit() and the end of complete(). */
                [[nodiscard]] bool run_in_progress() const noexcept override;

                /** @brief Refuses every later run. */
                void shutdown() override;

                /** @brief Skips the nodes of the run in progress that have not run yet. */
                void cancel() noexcept override;

                /** @brief Reports whether cancel() was called since the last reset_cancellation(). */
                bool is_cancelled() const override;

                /** @brief Clears the cancellation before a fresh run. */
                void reset_cancellation() override;

                /** @brief Returns the nodes run since the engine was made. */
                ExecutionMetrics metrics() const override;

                /** @brief Returns one floating worker, the thread that drives the executor. */
                WorkerPool worker_pool() const override;

                /** @brief Sizes the plan's ledger and restarts the step count. */
                void reset_profiling(Internal::Graph::CompiledGraph& graph,
                                     std::size_t expected_invocations = 1) override;

                /** @brief Fills an observation from the plan's ledger. */
                void collect_profiling(Internal::Graph::CompiledGraph& graph,
                                       RunObservation& observation) const override;

            private:
                /** @brief Posts the job that runs one ready node. */
                void post(Internal::Graph::Node& node);

                /** @brief Runs one node, records its time, and posts the successors it frees. */
                void dispatch(Internal::Graph::Node& node);

                /** @brief Ends the run when its sentinel is reached. */
                void finish_run();

                /** @brief Runs the body of one node; returns what it threw as an Error. */
                [[nodiscard]] Result<void> run_body(Internal::Graph::Node& node);

                /** @brief The executor every node runs on. */
                std::shared_ptr<SeededExecutor> executor_;

                /** @brief The clock every recorded dispatch is stamped from. */
                std::shared_ptr<const Clock> clock_;

                /** @brief The device count a profiling ledger is sized for. */
                std::size_t devices_;

                /** @brief Tells the context's observers about a run submit_observed() started. */
                std::function<void(const RunObservation&)> notify_;

                /** @brief The graph of the run in progress; null between runs. */
                Internal::Graph::CompiledGraph* running_ = nullptr;

                /** @brief The first failure of the run in progress. */
                std::optional<Error> failure_;

                /** @brief Whether the run in progress has reached its sentinel. */
                bool finished_ = true;

                /** @brief Whether the run in progress hands its observation over at its end. */
                bool observe_on_finish_ = false;

                /** @brief Whether cancel() was called since the last reset_cancellation(). */
                bool cancelled_ = false;

                /** @brief Whether shutdown() was called. */
                bool shut_down_ = false;

                /** @brief The nodes run since the engine was made. */
                std::size_t completed_ = 0;

                /** @brief The step of the run in progress, counted from the last reset_profiling(). */
                std::uint32_t step_ = 0;
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
