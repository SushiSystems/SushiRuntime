/**************************************************************************/
/* chain_run.cpp                                                          */
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
 * @file chain_run.cpp
 * @brief Submits a compiled chain plan to one device queue and records its events.
 * @author Mustafa Garip
 */

#include <chrono>
#include <vector>
#include <sycl/ext/oneapi/experimental/enqueue_functions.hpp>
#include <SushiRuntime/core/event_timestamps.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
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
              queue_(ctx.devices().queue(graph.chain.queue_index,
                                         Device::QueueFlavour{true, graph.profiling})),
              profiling_(graph.profiling)
        {
            // The plan owns the reserved event buffer (src/execution/README.md).
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
                    submit_node_discarding(*node);
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
         * @brief Submits one node's device work without keeping an event for it.
         *
         * The in-order queue carries the ordering a returned event would, and
         * the run waits once in @ref wait, so no caller reads a per-node event
         * on this path.
         */
        void ChainRun::submit_node_discarding(Graph::Node& node)
        {
            if (node.get_execution_mode() == Scheduler::Task::ExecutionMode::HOST)
            {
                std::vector<sycl::event> deps;
                deps.reserve(node.sycl_deps.size());
                for (const auto& ev : node.sycl_deps)
                    deps.push_back(ev);
                node.execute_host(queue_, deps);
                return;
            }

            sycl::ext::oneapi::experimental::submit(queue_, [&node](sycl::handler& h)
            {
                for (const auto& ev : node.sycl_deps)
                    h.depends_on(ev);
                node.execute(h);
            });
        }

        /**
         * @brief Submits one node and keeps its event for the ledger (src/execution/README.md).
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

            graph_.chain.pending_events.push_back(
                PendingEvent{&node, step, std::move(event), Graph::snapshot_widths(node)});
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
                rec.launched_width = pe.widths.launched;
                rec.live_width     = pe.widths.live;
                rec.submit_ns  = times.submit_ns;
                rec.start_ns   = times.start_ns;
                rec.end_ns     = times.end_ns;
                ledger.write(index, slot, rec);
            }
            graph_.chain.pending_events.clear();
        }

    } // namespace Execution
} // namespace SushiRuntime
