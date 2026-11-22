/**************************************************************************/
/* chain_replay.hpp                                                       */
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
 * @file chain_replay.hpp
 * @brief Declares ChainReplay, which replays a chained plan's recorded step once per step.
 * @author Mustafa Garip
 * @see modules/engine/execution/source/chain_replay.md
 */

#pragma once

#include <optional>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/recorded_steps.hpp>
#include <SushiRuntime/execution/chained_run_mark.hpp>
#include <SushiRuntime/profile/profile.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;

        /**
         * @brief Returns why a chained plan's nodes cannot be recorded as one command graph.
         * @param graph A plan whose chain.chainable is true.
         * @return A string literal naming the first refusal, or null when every node records.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API const char*
        replay_refusal(const Internal::Graph::CompiledGraph& graph) noexcept;

        /**
         * @brief Returns whether layer 2 runs on a device: one whose backend the spike or CI has
         *        replayed on and whose driver reports the oneAPI graph extension.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API bool replays_on(const sycl::device& device) noexcept;

        /**
         * @brief Settles once per plan whether a chained plan replays, recording its step the
         *        first time.
         * @param graph A plan whose chain.chainable is true.
         * @return Whether graph.chain.recorded holds the plan's recorded step.
         * @pre No run of the plan is in flight.
         * @see docs/design/GRAPH_REPLAY/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API bool prepare_replay(RuntimeContext& context,
                                                           Internal::Graph::CompiledGraph& graph);

        /**
         * @brief Replays a plan's recorded step from the calling thread, one submission per step.
         * @see modules/engine/execution/source/chain_replay.md
         */
        class SUSHIRUNTIME_API ChainReplay
        {
            public:
                /**
                 * @brief Binds the run to the plan's untimed in-order queue and marks it in progress.
                 * @param graph A plan prepare_replay() returned true for; must outlive this object.
                 */
                ChainReplay(RuntimeContext& context, Internal::Graph::CompiledGraph& graph);

                /**
                 * @brief Drains the queue unless finish() already did, logs the errors it held,
                 *        and ends the run for ExecutionEngine::run_in_progress().
                 */
                ~ChainReplay();

                ChainReplay(const ChainReplay&) = delete;
                ChainReplay& operator=(const ChainReplay&) = delete;
                ChainReplay(ChainReplay&&) = delete;
                ChainReplay& operator=(ChainReplay&&) = delete;

                /**
                 * @brief Submits one replay of the recorded step without waiting, after the
                 *        replay before it when the recorded step reads host words.
                 * @return Nothing, or what the submission or that wait threw, as an error.
                 */
                [[nodiscard]] Result<void> submit_step();

                /**
                 * @brief Blocks until every submitted step has finished.
                 * @return Nothing, or the first error the queue held.
                 */
                [[nodiscard]] Result<void> wait();

                /**
                 * @brief Waits once for the whole run; the run is over when this returns.
                 * @return What wait() returns.
                 */
                [[nodiscard]] Result<void> finish();

            private:
                Internal::Graph::RecordedSteps& recorded_; /**< The plan's recorded step. */
                Profile::Collector& collector_;   /**< The context's collector this run's zones record into. */
                sycl::queue& queue_;              /**< Untimed in-order queue on the plan's device. */
                std::optional<sycl::event> last_; /**< The replay in flight, kept only when replays read host words. */
                bool finished_ = false;           /**< Whether finish() drained the queue. */
                ChainedRunMark run_mark_;         /**< The run's mark; last, so it outlasts the drain. */
        };

    } // namespace Execution
} // namespace SushiRuntime
