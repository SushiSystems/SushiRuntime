/**************************************************************************/
/* host_execution_engine.cpp                                              */
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
 * @file host_execution_engine.cpp
 * @brief Defines HostExecutionEngine, which runs a compiled graph on a SeededExecutor.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#include <SushiRuntime/execution/host_execution_engine.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

#include "run_observation_fill.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Returns the refusal of a node the host engine cannot run. */
            [[nodiscard]] Error no_host_form() noexcept
            {
                return Error{ErrorCode::Unsupported,
                             "the node has no host form: a host runtime runs host nodes and launches "
                             "over a count or an Extent; run an NdRange, a raw command group or a "
                             "queue body on a runtime that has a device"};
            }
        } // namespace

        HostExecutionEngine::HostExecutionEngine(std::shared_ptr<SeededExecutor> executor,
                                                 std::shared_ptr<const Clock> clock,
                                                 std::size_t devices,
                                                 std::function<void(const RunObservation&)> notify)
            : executor_(std::move(executor)),
              clock_(std::move(clock)),
              devices_(devices),
              notify_(std::move(notify))
        {
            SR_VERIFY(executor_ != nullptr, "HostExecutionEngine: pass an executor, not null.");
            SR_VERIFY(clock_ != nullptr, "HostExecutionEngine: pass a clock, not null.");
            SR_VERIFY(devices_ > 0, "HostExecutionEngine: pass a device count of at least 1.");
        }

        Result<void> HostExecutionEngine::execute(Internal::Graph::CompiledGraph& graph)
        {
            Result<void> submitted = submit(graph);
            if (!submitted)
                return submitted;
            return complete();
        }

        Result<void> HostExecutionEngine::submit(Internal::Graph::CompiledGraph& graph)
        {
            if (shut_down_)
                return make_error(Error{ErrorCode::InvalidState, "Execute called after shutdown."});
            if (graph.roots.empty())
                return make_error(Error{ErrorCode::InvalidGraph,
                                        "Received a compiled graph with no root nodes."});
            if (running_ != nullptr)
                return make_error(Error{ErrorCode::InvalidState,
                                        "ExecutionEngine::submit() is not reentrant: a graph run is "
                                        "already in progress on this engine."});
            running_ = &graph;
            failure_.reset();
            finished_ = false;
            for (Internal::Graph::Node* root : graph.roots)
                post(*root);
            return {};
        }

        Result<void> HostExecutionEngine::submit_observed(Internal::Graph::CompiledGraph& graph)
        {
            observe_on_finish_ = true;
            Result<void> submitted = submit(graph);
            if (!submitted)
                observe_on_finish_ = false;
            return submitted;
        }

        Result<void> HostExecutionEngine::complete()
        {
            SR_VERIFY(running_ != nullptr,
                      "HostExecutionEngine::complete(): no run was submitted; call submit() first.");
            while (!finished_)
                SR_VERIFY(executor_->run_one(),
                          "HostExecutionEngine::complete(): the executor holds no job and the run "
                          "has not reached its last node; a job was dropped outside the engine.");
            running_ = nullptr;
            if (failure_)
                return make_error(std::move(*failure_));
            return {};
        }

        bool HostExecutionEngine::run_finished()
        {
            return finished_;
        }

        bool HostExecutionEngine::run_in_progress() const noexcept
        {
            return running_ != nullptr;
        }

        void HostExecutionEngine::shutdown()
        {
            shut_down_ = true;
        }

        void HostExecutionEngine::cancel() noexcept
        {
            cancelled_ = true;
        }

        bool HostExecutionEngine::is_cancelled() const
        {
            return cancelled_;
        }

        void HostExecutionEngine::reset_cancellation()
        {
            cancelled_ = false;
        }

        ExecutionMetrics HostExecutionEngine::metrics() const
        {
            ExecutionMetrics metrics;
            metrics.tasks_completed = completed_;
            return metrics;
        }

        WorkerPool HostExecutionEngine::worker_pool() const
        {
            WorkerPool pool;
            pool.count = 1;
            pool.pinning = PinPolicy::Float;
            return pool;
        }

        void HostExecutionEngine::reset_profiling(Internal::Graph::CompiledGraph& graph,
                                                  std::size_t expected_invocations)
        {
            step_ = 0;
            graph.profiling_ledger.reserve(graph.profiling_multiplicities,
                                           static_cast<std::uint32_t>(expected_invocations),
                                           devices_);
        }

        void HostExecutionEngine::collect_profiling(Internal::Graph::CompiledGraph& graph,
                                                    RunObservation& observation) const
        {
            fill_observation(graph, observation);
        }

        void HostExecutionEngine::post(Internal::Graph::Node& node)
        {
            Internal::Graph::Node* const target = &node;
            executor_->post([this, target]() { dispatch(*target); });
        }

        void HostExecutionEngine::dispatch(Internal::Graph::Node& node)
        {
            if (node.is_sentinel)
            {
                finish_run();
                return;
            }

            const bool skipped = failure_.has_value() || cancelled_ ||
                                 (node.bindings_ && !node.bindings_->enabled.load());
            if (!skipped)
            {
                const std::uint64_t start = clock_->now_ns();
                Result<void> ran = run_body(node);
                const std::uint64_t end = clock_->now_ns();
                if (!ran)
                    failure_ = Internal::Scheduler::name_failure(node, ran.error());
                else if (running_->profiling)
                {
                    Internal::Observability::DispatchFacts facts;
                    facts.node_index = node.registration_index;
                    facts.invocation = step_;
                    running_->profiling_ledger.record(
                        facts, Internal::Observability::DispatchTimes{start, start, end});
                }
                ++completed_;
            }

            // Every task a graph node frees is a graph node.
            Internal::Scheduler::ReadySink freed;
            node.on_complete(freed);
            for (Internal::Scheduler::Task* successor : freed)
                post(*static_cast<Internal::Graph::Node*>(successor));
        }

        void HostExecutionEngine::finish_run()
        {
            finished_ = true;
            ++step_;
            if (!observe_on_finish_)
                return;
            observe_on_finish_ = false;
            if (failure_ || !notify_)
                return;
            RunObservation& observation = running_->observation;
            observation.layer = ExecutionLayer::Scheduler;
            if (running_->profiling)
                fill_observation(*running_, observation);
            else
                clear_observation(*running_, observation);
            notify_(observation);
        }

        Result<void> HostExecutionEngine::run_body(Internal::Graph::Node& node)
        {
            const bool native = node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Host &&
                                !node.submits_device_work();
            if (native)
                return SYCL::translate_exceptions([&node]() { node.execute_native(); });
            if (node.host_form)
                return SYCL::translate_exceptions([&node]() { node.host_form(); });
            return make_error(no_host_form());
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
