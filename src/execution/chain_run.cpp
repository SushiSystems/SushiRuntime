/**************************************************************************/
/* chain_run.cpp                                                          */
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

#include <chrono>
#include <vector>
#include <SushiRuntime/core/event_timestamps.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        namespace
        {
            /**
             * @brief Converts a steady-clock time point to nanoseconds since its epoch.
             * @param t The time point.
             * @return Nanoseconds since the steady clock's epoch.
             */
            std::uint64_t steady_ns(std::chrono::steady_clock::time_point t) noexcept
            {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
            }
        } // namespace

        /**
         * @brief Binds the run to the plan's device queue of the right flavour.
         */
        ChainRun::ChainRun(RuntimeContext& ctx, CompiledGraph& graph)
            : graph_(graph),
              queue_(ctx.chain_queue(graph.chain.queue_index, graph.profiling)),
              profiling_(graph.profiling)
        {
            // Reserved on the plan, not here: a fresh ChainRun binds to the same
            // plan every tick, so growing graph_.chain.pending_events once and
            // reusing it keeps later runs allocation-free instead of paying for a
            // new buffer every time this short-lived object is built.
            if (profiling_ && graph.chain.pending_events.capacity() < graph.chain.order.size())
                graph.chain.pending_events.reserve(graph.chain.order.size());
        }

        /**
         * @brief Drains the queue unless finish() already did, swallowing errors.
         */
        ChainRun::~ChainRun()
        {
            if (finished_)
                return;
            try
            {
                queue_.wait();
            }
            catch (const std::exception& e)
            {
                SR_LOG_WARN("Device error while draining an abandoned chain run: {}", e.what());
            }
            catch (...)
            {
                SR_LOG_WARN("Unknown device error while draining an abandoned chain run.");
            }
        }

        /**
         * @brief Submits one step's enabled nodes in compiled order without waiting.
         */
        void ChainRun::submit_step(std::uint32_t step)
        {
            for (Graph::Node* node : graph_.chain.order)
            {
                // The step machinery wrote enabled on this thread before this step.
                if (node->bindings_ && !node->bindings_->enabled.load(std::memory_order_relaxed))
                    continue;

                if (profiling_)
                    submit_profiled(*node, step);
                else
                    submit_node(*node);
            }
        }

        /**
         * @brief Blocks until every submitted step has finished and rethrows a device error.
         */
        void ChainRun::wait()
        {
            queue_.wait_and_throw();
        }

        /**
         * @brief Waits once for the whole run, then writes its event records into the ledger.
         */
        void ChainRun::finish()
        {
            wait();
            finished_ = true;
            if (profiling_)
                record_events();
        }

        /**
         * @brief Submits one node's device work to the queue.
         */
        sycl::event ChainRun::submit_node(Graph::Node& node)
        {
            if (node.get_execution_mode() == Scheduler::Task::ExecutionMode::HOST)
            {
                std::vector<sycl::event> deps;
                deps.reserve(node.sycl_deps.size());
                for (const auto& ev : node.sycl_deps)
                    deps.push_back(ev);
                return node.execute_host(queue_, deps);
            }

            return queue_.submit([&node](sycl::handler& h)
            {
                for (const auto& ev : node.sycl_deps)
                    h.depends_on(ev);
                node.execute(h);
            });
        }

        /**
         * @brief Submits one node and keeps its event for the ledger.
         *
         * The run's first kernel submission claims the clock pairing, as the
         * scheduler's first device submission does. Unlike the scheduler, this
         * path never keeps an inline host node (chain_plan.hpp's
         * node_kind_is_chainable precondition admits only a KERNEL node or a
         * HOST node that submits device work), so an empty event here would be a
         * rare, unexpected case rather than the routine one the scheduler
         * guards against; the event is kept unconditionally and
         * record_events()'s read_event_timestamps() already drops an empty or
         * unreadable one on its own, so no per-submission check is needed here.
         */
        void ChainRun::submit_profiled(Graph::Node& node, std::uint32_t step)
        {
            Core::ProfilingLedger& ledger = graph_.profiling_ledger;
            const Scheduler::Task* task = &node;
            const bool is_kernel = node.get_execution_mode() == Scheduler::Task::ExecutionMode::KERNEL;
            const bool owns_pairing = is_kernel && !pairing_claimed_ && ledger.claim_pairing_owner(task);
            if (is_kernel)
                pairing_claimed_ = true;

            const auto t0 = std::chrono::steady_clock::now();
            sycl::event event = submit_node(node);
            if (owns_pairing)
            {
                const auto t1 = std::chrono::steady_clock::now();
                ledger.set_pairing_submit_window(steady_ns(t0), steady_ns(t1) - steady_ns(t0));
            }

            graph_.chain.pending_events.push_back(PendingEvent{&node, step, std::move(event)});
        }

        /**
         * @brief Writes every kept event's timestamps into the plan's ledger.
         */
        void ChainRun::record_events()
        {
            Core::ProfilingLedger& ledger = graph_.profiling_ledger;
            for (const PendingEvent& pe : graph_.chain.pending_events)
            {
                Core::EventTimestamps times;
                if (!Core::read_event_timestamps(pe.event, times))
                    continue;

                const Scheduler::Task* task = pe.node;
                if (times.has_submit && ledger.is_pairing_owner(task))
                    ledger.set_pairing_event_ns(times.submit_ns);

                const std::uint32_t index = pe.node->registration_index;
                const std::uint32_t slot = ledger.claim_slot(index);
                if (slot == Core::ProfilingLedger::npos)
                    continue;

                Core::NodeEventRecord rec{};
                rec.node_index = index;
                rec.invocation = pe.step;
                rec.submit_ns  = times.submit_ns;
                rec.start_ns   = times.start_ns;
                rec.end_ns     = times.end_ns;
                ledger.write(index, slot, rec);
            }
            graph_.chain.pending_events.clear();
        }

    } // namespace Execution
} // namespace SushiRuntime
