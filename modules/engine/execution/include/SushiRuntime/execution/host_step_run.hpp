/**************************************************************************/
/* host_step_run.hpp                                                      */
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
 * @file host_step_run.hpp
 * @brief Declares HostStepRun, which runs a plan's steps on the calling thread.
 * @author Mustafa Garip
 * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/ready_sink.hpp>

#include <SushiRuntime/execution/chained_run_mark.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;

        /**
         * @brief Runs a plan's steps on the calling thread through Node::run_on_host(), one
         *        node at a time in the order the dependencies free them.
         * @see docs/design/LAYERED_EXECUTION/RUN_PLACEMENT.md
         */
        class SUSHIRUNTIME_API HostStepRun
        {
            public:
                /**
                 * @brief Binds the run to the plan and marks it in progress on the context's engine.
                 * @param graph A plan host_step_refusal() accepted; must outlive this object.
                 */
                HostStepRun(RuntimeContext& context, Internal::Graph::CompiledGraph& graph);

                HostStepRun(const HostStepRun&) = delete;
                HostStepRun& operator=(const HostStepRun&) = delete;
                HostStepRun(HostStepRun&&) = delete;
                HostStepRun& operator=(HostStepRun&&) = delete;

                /** @brief Ends the run for ExecutionEngine::run_in_progress(). */
                ~HostStepRun() = default;

                /**
                 * @brief Runs one step's enabled nodes to completion before it returns.
                 * @param step Zero-based step index within the run, stamped into dispatch records.
                 * @pre The plan was re-armed for this step.
                 * @return Nothing, or what a node threw as an error that names the node; the
                 *         nodes it would have freed do not run.
                 */
                [[nodiscard]] Result<void> submit_step(std::uint32_t step);

                /** @brief Returns at once: every step finished inside submit_step(). */
                [[nodiscard]] Result<void> wait() noexcept;

                /** @brief Ends the run for ExecutionEngine::run_in_progress(). */
                [[nodiscard]] Result<void> finish() noexcept;

            private:
                /**
                 * @brief Runs one node and records its dispatch when the run is timed.
                 * @return What the node's host work returned.
                 */
                Result<void> run_node(Internal::Graph::Node& node, std::uint32_t step);

                Internal::Graph::CompiledGraph& graph_;       /**< The plan this run walks. */
                Profile::Collector& collector_;              /**< The collector this run's zones record into. */
                const Clock& clock_;                         /**< The engine's clock, which stamps timed dispatches. */
                Internal::Scheduler::ReadySink ready_;    /**< Nodes freed and not yet run; keeps its capacity. */
                Internal::Scheduler::ReadySink freed_;    /**< Successors one node's completion frees. */
                ChainedRunMark run_mark_;                    /**< The run's mark on the engine. */
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
