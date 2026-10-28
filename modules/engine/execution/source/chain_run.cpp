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

#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/observability/event_timestamps.hpp>
#include <SushiRuntime/observability/pairing_claim.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/task_submission.hpp>
#include <SushiRuntime/execution/chain_run.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Binds the run to the plan's device queue of the right flavour.
         */
        ChainRun::ChainRun(RuntimeContext& context, Internal::Graph::CompiledGraph& graph)
            : graph_(graph),
              queue_(context.devices().queue(graph.chain.queue_index,
                                         Internal::Device::QueueFlavour{true, graph.profiling})),
              profiling_(graph.profiling),
              run_mark_(context.execution_engine())
        {
            // An abandoned run leaves its events on the plan; clear() keeps the capacity.
            graph.chain.pending_events.clear();

            // The plan owns the reserved event buffer (modules/engine/execution/README.md).
            if (profiling_ && graph.chain.pending_events.capacity() < graph.chain.order.size())
                graph.chain.pending_events.reserve(graph.chain.order.size());
        }

        /** @brief Marks a chained run in progress on @p engine. */
        ChainRun::RunMark::RunMark(ExecutionEngine& engine) noexcept : engine_(&engine)
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
         * @brief Drains the queue unless finish() already did, and logs the errors it held.
         */
        ChainRun::~ChainRun()
        {
            if (finished_)
                return;

            // wait_and_throw() takes the queue's asynchronous errors, so the next run meets none.
            const Result<void> drained =
                SYCL::translate_exceptions([this]() { queue_.wait_and_throw(); });
            if (!drained)
                SR_LOG_WARN(K_EXECUTION_LOG_CATEGORY, "Device error while draining an abandoned chain run: {}",
                            drained.error().message);
        }

        /**
         * @brief Submits one step's enabled nodes in compiled order without waiting.
         */
        Result<void> ChainRun::submit_step(std::uint32_t step)
        {
            SR_ZONE("chain.submit_step");
            for (Internal::Graph::Node* node : graph_.chain.order)
            {
                // The step machinery wrote enabled on this thread before this step.
                if (node->bindings_ &&
                    !node->bindings_->enabled.load(std::memory_order_relaxed))
                    continue;

                Result<void> submitted = SYCL::translate_exceptions([this, node, step]()
                {
                    if (profiling_)
                        submit_profiled(*node, step);
                    else
                        submit_node_discarding(*node);
                });
                if (!submitted)
                    return make_error(
                        Internal::Scheduler::name_failure(*node, submitted.error()));
            }
            return {};
        }

        /**
         * @brief Blocks until every submitted step has finished.
         */
        Result<void> ChainRun::wait()
        {
            SR_ZONE("chain.wait");
            // The asynchronous handler rethrows here, where the wait is.
            return SYCL::translate_exceptions([this]() { queue_.wait_and_throw(); });
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
        sycl::event ChainRun::submit_node(Internal::Graph::Node& node, std::vector<sycl::event>& handed_over)
        {
            return Internal::Graph::submit_task(queue_, node, &node.sycl_deps, &handed_over);
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
            Internal::Graph::submit_task_discarding(queue_, node, &node.sycl_deps);
        }

        /**
         * @brief Submits one node and keeps its event for the ledger (modules/engine/execution/README.md).
         */
        void ChainRun::submit_profiled(Internal::Graph::Node& node, std::uint32_t step)
        {
            Internal::Observability::ProfilingLedger& ledger = graph_.profiling_ledger;
            const Internal::Scheduler::Task* task = &node;
            const std::uint32_t device = static_cast<std::uint32_t>(graph_.chain.queue_index);
            // A task body submits device work as a kernel node does, so the first node of either kind claims.
            Internal::Observability::PairingClaim claim(pairing_claimed_ ? nullptr : &ledger, device, task);
            pairing_claimed_ = true;

            std::vector<sycl::event> handed_over;
            sycl::event event = submit_node(node, handed_over);
            // A body that submitted nothing has no event to pair, so the next node claims instead.
            const bool host = node.execution_mode() == Internal::Scheduler::Task::ExecutionMode::Host;
            if (claim.settle(Internal::Scheduler::submitted_device_work(host, true, event, handed_over)))
                pairing_claimed_ = false;

            graph_.chain.pending_events.push_back(
                Internal::Graph::PendingEvent{&node, step, std::move(event), Internal::Graph::snapshot_widths(node),
                                              Internal::Profile::current_thread_index(), std::move(handed_over)});
        }

        /**
         * @brief Writes every kept event's timestamps into the plan's ledger.
         */
        void ChainRun::record_events()
        {
            SR_ZONE_FINE("chain.record_events");

            Internal::Observability::ProfilingLedger& ledger = graph_.profiling_ledger;
            const std::uint32_t device = static_cast<std::uint32_t>(graph_.chain.queue_index);
            for (const Internal::Graph::PendingEvent& pe : graph_.chain.pending_events)
            {
                const std::uint32_t index = pe.node->registration_index;
                Internal::Observability::EventTimestamps times;
                if (!Internal::Observability::read_dispatch_timestamps(pe.event, pe.handed_over, times))
                {
                    ledger.note_drop(DropCause::UnreadableEvent, index);
                    continue;
                }

                const Internal::Scheduler::Task* task = pe.node;
                if (times.has_submit && ledger.is_pairing_owner(device, task))
                    ledger.set_pairing_event_ns(device, times.submit_ns);

                Internal::Observability::DispatchFacts facts;
                facts.node_index = index;
                facts.invocation = pe.step;
                facts.launched_width = pe.widths.launched;
                facts.live_width = pe.widths.live;
                facts.device_index = device;
                facts.submit_thread = pe.submit_thread;
                ledger.record(facts, Internal::Observability::DispatchTimes{times.submit_ns, times.start_ns, times.end_ns});
            }
            graph_.chain.pending_events.clear();
        }

    } // namespace Execution
} // namespace SushiRuntime
