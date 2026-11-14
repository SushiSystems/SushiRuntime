/**************************************************************************/
/* host_step_run.cpp                                                      */
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
 * @file host_step_run.cpp
 * @brief Defines HostStepRun, which runs a plan's steps on the calling thread.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#include <SushiRuntime/execution/host_step_run.hpp>

#include <atomic>
#include <cstdint>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>

#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        HostStepRun::HostStepRun(RuntimeContext& context, Internal::Graph::CompiledGraph& graph)
            : graph_(graph),
              collector_(context.profile_collector()),
              clock_(context.execution_engine().clock()),
              run_mark_(context.execution_engine())
        {
            ready_.reserve(graph.all_nodes.size());
        }

        Result<void> HostStepRun::submit_step(std::uint32_t step)
        {
            SR_ZONE(collector_, "host.step");
            const Internal::Graph::Node* const sentinel = graph_.sentinel.get();
            ready_.assign(graph_.roots.begin(), graph_.roots.end());
            while (!ready_.empty())
            {
                auto* const node = static_cast<Internal::Graph::Node*>(ready_.back());
                ready_.pop_back();
                if (node == sentinel)
                    continue;

                // The step machinery wrote enabled on this thread before this step.
                const bool enabled = !node->bindings_ ||
                                     node->bindings_->enabled.load(std::memory_order_relaxed);
                if (enabled)
                {
                    Result<void> ran = run_node(*node, step);
                    if (!ran)
                        return make_error(Internal::Scheduler::name_failure(*node, ran.error()));
                }

                // A disabled node frees its successors as a completed one does.
                freed_.clear();
                node->on_complete(freed_);
                ready_.insert(ready_.end(), freed_.begin(), freed_.end());
            }
            return {};
        }

        Result<void> HostStepRun::wait() noexcept
        {
            return {};
        }

        Result<void> HostStepRun::finish() noexcept
        {
            run_mark_.release();
            return {};
        }

        Result<void> HostStepRun::run_node(Internal::Graph::Node& node, std::uint32_t step)
        {
            if (!graph_.profiling)
                return node.run_on_host();

            const Internal::Graph::NodeWidths widths = node.submits_device_work()
                ? Internal::Graph::snapshot_widths(node) : Internal::Graph::NodeWidths{};
            const std::uint64_t start = clock_.now_ns();
            Result<void> ran = node.run_on_host();
            const std::uint64_t end = clock_.now_ns();
            if (!ran)
                return ran;

            Internal::Observability::DispatchFacts facts;
            facts.node_index = node.registration_index;
            facts.invocation = step;
            facts.launched_width = widths.launched;
            facts.live_width = widths.live;
            facts.device_index = K_HOST_DEVICE_INDEX;
            facts.submit_thread = Internal::Profile::current_thread_index();
            graph_.profiling_ledger.record(facts, Internal::Observability::DispatchTimes{start, start, end});
            return ran;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
