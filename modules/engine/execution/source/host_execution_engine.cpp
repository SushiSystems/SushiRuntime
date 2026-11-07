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
 * @brief Defines HostExecutionEngine, which runs a compiled graph as jobs of an Executor.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#include "host_execution_engine.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

#include "run_observation_fill.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        HostExecutionEngine::HostExecutionEngine(std::shared_ptr<Executor> executor,
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
            failed_.store(false);
            finished_.store(false);
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
            // A job's count is its last step, so at 0 after the sentinel no job reads the run.
            while (!run_finished())
                if (!executor_->run_one())
                    std::this_thread::yield();
            running_ = nullptr;
            const std::lock_guard<std::mutex> lock(mutex_);
            if (failure_)
                return make_error(std::move(*failure_));
            return {};
        }

        bool HostExecutionEngine::run_finished()
        {
            return finished_.load() && outstanding_.load() == 0;
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
            cancelled_.store(true);
        }

        bool HostExecutionEngine::is_cancelled() const
        {
            return cancelled_.load();
        }

        void HostExecutionEngine::reset_cancellation()
        {
            cancelled_.store(false);
        }

        ExecutionMetrics HostExecutionEngine::metrics() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
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
            outstanding_.fetch_add(1);
            executor_->post([this, target]() { dispatch(*target); });
        }

        void HostExecutionEngine::dispatch(Internal::Graph::Node& node)
        {
            // The sentinel is told by its address: only the scheduler engine marks it.
            if (&node == running_->sentinel.get())
            {
                finish_run();
                outstanding_.fetch_sub(1);
                return;
            }

            const bool skipped = failed_.load() || cancelled_.load() ||
                                 (node.bindings_ && !node.bindings_->enabled.load());
            if (!skipped)
            {
                // A node that submits work states its widths, as its dispatch would on a device.
                const Internal::Graph::NodeWidths widths = node.submits_device_work()
                    ? Internal::Graph::snapshot_widths(node) : Internal::Graph::NodeWidths{};
                const std::uint64_t start = clock_->now_ns();
                const Result<void> ran = node.run_on_host();
                const std::uint64_t end = clock_->now_ns();
                record(node, ran, widths.launched, widths.live, start, end);
            }

            // Every task a graph node frees is a graph node; each is counted before this one ends.
            Internal::Scheduler::ReadySink freed;
            node.on_complete(freed);
            for (Internal::Scheduler::Task* successor : freed)
                post(*static_cast<Internal::Graph::Node*>(successor));
            outstanding_.fetch_sub(1);
        }

        void HostExecutionEngine::record(Internal::Graph::Node& node, const Result<void>& ran,
                                         std::uint32_t launched, std::uint32_t live,
                                         std::uint64_t start, std::uint64_t end)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++completed_;
            if (!ran)
            {
                if (!failure_)
                    failure_ = Internal::Scheduler::name_failure(node, ran.error());
                failed_.store(true);
                return;
            }
            if (!running_->profiling)
                return;
            Internal::Observability::DispatchFacts facts;
            facts.node_index = node.registration_index;
            facts.invocation = step_;
            facts.launched_width = launched;
            facts.live_width = live;
            running_->profiling_ledger.record(
                facts, Internal::Observability::DispatchTimes{start, start, end});
        }

        void HostExecutionEngine::finish_run()
        {
            bool notify = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                ++step_;
                notify = observe_on_finish_ && !failure_ && notify_ != nullptr;
                observe_on_finish_ = false;
                if (notify)
                {
                    RunObservation& observation = running_->observation;
                    observation.layer = ExecutionLayer::Scheduler;
                    if (running_->profiling)
                        fill_observation(*running_, observation);
                    else
                        clear_observation(*running_, observation);
                }
            }
            if (notify)
                notify_(running_->observation);
            finished_.store(true);
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
