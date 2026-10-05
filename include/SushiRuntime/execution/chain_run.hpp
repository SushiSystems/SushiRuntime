/****************************************************************/
/* chain_run.hpp                                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstdint>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        class RuntimeContext;

        /**
         * @brief Submits a chainable plan's steps from the calling thread to one in-order queue.
         *
         * One object lives for one run. Each submit_step() walks the plan's
         * compiled order and submits every enabled node with no scheduler
         * hand-off and no polling; the queue's order stands in for the DAG's
         * edges. finish() is the run's single wait. With profiling on, every
         * submission's event becomes a NodeEventRecord in the plan's ledger.
         * The destructor drains the queue, so a throwing run never leaves work
         * in flight against a plan about to be destroyed.
         */
        class SUSHIRUNTIME_API ChainRun
        {
            public:
                /**
                 * @brief Binds the run to the plan's device queue of the right flavour.
                 * @param ctx   The context that owns the in-order queues.
                 * @param graph A plan whose chain.chainable is true; must outlive this object.
                 */
                ChainRun(RuntimeContext& ctx, CompiledGraph& graph);

                /** @brief Drains the queue unless finish() already did, swallowing errors. */
                ~ChainRun();

                ChainRun(const ChainRun&) = delete;
                ChainRun& operator=(const ChainRun&) = delete;

                /**
                 * @brief Submits one step's enabled nodes in compiled order without waiting.
                 * @param step Zero-based step index within the run, stamped into event records.
                 */
                void submit_step(std::uint32_t step);

                /** @brief Blocks until every submitted step has finished and rethrows a device error. */
                void wait();

                /** @brief Waits once for the whole run, then writes its event records into the ledger. */
                void finish();

            private:
                /**
                 * @brief Submits one node's device work to the queue.
                 * @param node The node to submit.
                 * @return The submission's event, or an empty event when host work submitted nothing.
                 */
                sycl::event submit_node(Graph::Node& node);

                /**
                 * @brief Submits one node's device work without keeping an event for it.
                 * @param node The node to submit.
                 */
                void submit_node_discarding(Graph::Node& node);

                /**
                 * @brief Submits one node and keeps its event for the ledger.
                 * @param node The node to submit.
                 * @param step Zero-based step index within the run.
                 */
                void submit_profiled(Graph::Node& node, std::uint32_t step);

                /** @brief Writes every kept event's timestamps into the plan's ledger. */
                void record_events();

                CompiledGraph& graph_;          /**< The plan this run submits. */
                sycl::queue& queue_;            /**< In-order queue on the plan's device. */
                bool profiling_;                /**< Whether the plan requested profiling. */
                bool pairing_claimed_ = false;  /**< Whether this run already tried the clock pairing. */
                bool finished_ = false;         /**< Whether finish() drained the queue. */
        };

    } // namespace Execution
} // namespace SushiRuntime
