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
#include <SushiRuntime/observability/event_timestamps.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /**
             * @brief Runs work and returns what it throws as a KernelFailed error.
             * @return Nothing when work returned.
             */
            template <typename Work>
            [[nodiscard]] Result<void> kernel_failure_of(Work&& work)
            {
                try
                {
                    work();
                    return {};
                }
                catch (const std::exception& cause)
                {
                    return make_error(Error{ErrorCode::KernelFailed, cause.what()});
                }
                catch (...)
                {
                    return make_error(Error{ErrorCode::KernelFailed,
                        "A chained step threw an object that is not a std::exception."});
                }
            }

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
        ChainRun::ChainRun(RuntimeContext& context, CompiledGraph& graph)
            : graph_(graph),
              queue_(context.devices().queue(graph.chain.queue_index,
                                         Internal::Device::QueueFlavour{true, graph.profiling})),
              profiling_(graph.profiling),
              // RuntimeContext builds an SchedulerExecutionEngine and no other engine, so the cast holds.
              run_mark_(static_cast<SchedulerExecutionEngine&>(context.execution_engine()))
        {
            // The plan owns the reserved event buffer (modules/engine/execution/README.md).
            if (profiling_ && graph.chain.pending_events.capacity() < graph.chain.order.size())
                graph.chain.pending_events.reserve(graph.chain.order.size());
        }

        /** @brief Marks a chained run in progress on @p engine. */
        ChainRun::RunMark::RunMark(SchedulerExecutionEngine& engine) noexcept : engine_(&engine)
        {
            engine_->begin_chained_run();
        }

        /** @brief Clears the mark unless release() already did. */
        ChainRun::RunMark::~RunMark()
        {
            release();
        }

        /** @brief Clears the mark; a second call does nothing. */
        void ChainRun::RunMark::release() noexcept
        {
            if (engine_ == nullptr)
                return;
            engine_->end_chained_run();
            engine_ = nullptr;
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
        Result<void> ChainRun::submit_step(std::uint32_t step)
        {
            return kernel_failure_of([this, step]()
            {
                for (Internal::Graph::Node* node : graph_.chain.order)
                {
                    // The step machinery wrote enabled on this thread before this step.
                    if (node->bindings_ &&
                        !node->bindings_->enabled.load(std::memory_order_relaxed))
                        continue;

                    if (profiling_)
                        submit_profiled(*node, step);
                    else
                        submit_node_discarding(*node);
                }
            });
        }

        /**
         * @brief Blocks until every submitted step has finished.
         */
        Result<void> ChainRun::wait()
        {
            // The asynchronous handler rethrows here, where the wait is.
            return kernel_failure_of([this]() { queue_.wait_and_throw(); });
        }

        /**
         * @brief Waits once for the whole run, then writes its event records into the ledger.
         */
        Result<void> ChainRun::finish()
        {
            /** @brief Releases the run mark when finish() returns. */
            struct MarkRelease
            {
                RunMark& mark; /**< The mark to release. */

                /** @brief Releases the mark. */
                ~MarkRelease()
                {
                    mark.release();
                }
            } mark_release{run_mark_};

            Result<void> waited = wait();
            if (!waited)
                return waited;
            finished_ = true;
            if (profiling_)
                record_events();
            return {};
        }

        /**
         * @brief Submits one node's device work to the queue.
         */
        sycl::event ChainRun::submit_node(Internal::Graph::Node& node)
        {
            if (node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Host)
            {
                std::vector<sycl::event> dependencies;
                dependencies.reserve(node.sycl_deps.size());
                for (const auto& ev : node.sycl_deps)
                    dependencies.push_back(ev);
                return node.execute_host(queue_, dependencies);
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
        void ChainRun::submit_node_discarding(Internal::Graph::Node& node)
        {
            if (node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Host)
            {
                std::vector<sycl::event> dependencies;
                dependencies.reserve(node.sycl_deps.size());
                for (const auto& ev : node.sycl_deps)
                    dependencies.push_back(ev);
                node.execute_host(queue_, dependencies);
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
         * @brief Submits one node and keeps its event for the ledger (modules/engine/execution/README.md).
         */
        void ChainRun::submit_profiled(Internal::Graph::Node& node, std::uint32_t step)
        {
            Internal::Observability::ProfilingLedger& ledger = graph_.profiling_ledger;
            const Internal::Scheduler::Task* task = &node;
            const bool is_kernel = node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Kernel;
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
                PendingEvent{&node, step, std::move(event), Internal::Graph::snapshot_widths(node)});
        }

        /**
         * @brief Writes every kept event's timestamps into the plan's ledger.
         */
        void ChainRun::record_events()
        {
            Internal::Observability::ProfilingLedger& ledger = graph_.profiling_ledger;
            for (const PendingEvent& pe : graph_.chain.pending_events)
            {
                Internal::Observability::EventTimestamps times;
                if (!Internal::Observability::read_event_timestamps(pe.event, times))
                    continue;

                const Internal::Scheduler::Task* task = pe.node;
                if (times.has_submit && ledger.is_pairing_owner(task))
                    ledger.set_pairing_event_ns(times.submit_ns);

                const std::uint32_t index = pe.node->registration_index;
                const std::uint32_t slot = ledger.claim_slot(index);
                if (slot == Internal::Observability::ProfilingLedger::npos)
                    continue;

                NodeEventRecord rec{};
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
