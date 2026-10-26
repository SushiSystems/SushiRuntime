/**************************************************************************/
/* event_polling.hpp                                                      */
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
 * @file event_polling.hpp
 * @brief Declares EventPolling, which polls SYCL events and releases completed tasks.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/scheduler/quiescence_latch.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/ready_sink.hpp>
#include <SushiRuntime/observability/event_timestamps.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Polls hardware events in a lock-free manner.
         */
        class EventPolling 
        {
            public:
                /** @brief Structure to link a SYCL event with a graph node or task. */
                struct PolledEvent
                {
                    sycl::event event;
                    SushiRuntime::Internal::Scheduler::Task* task = nullptr;
                    /**
                     * @brief Marks synchronous host work as already complete.
                     * @see modules/engine/scheduler/README.md
                     */
                    bool ready = false;

                    /**
                     * @brief Tells whether event is a real SYCL event, cached at push time.
                     * @see modules/engine/scheduler/README.md
                     */
                    bool has_event = false;

                    /**
                     * @brief Tells whether the task's work ran to its end; false when skipped or thrown.
                     * @see modules/engine/scheduler/README.md
                     */
                    bool executed = true;

                    /** @brief Run step this dispatch belongs to, captured by the submitting worker. */
                    std::uint32_t step = 0;

                    /** @brief The dispatched node's widths, snapshotted where the event is armed. */
                    SushiRuntime::Internal::Graph::NodeWidths widths{};

                    /** @brief The events the task's body handed over, when it handed over any. */
                    std::vector<sycl::event> handed_over;

                    /** @brief Device whose queue the dispatch went to, or K_HOST_DEVICE_INDEX. */
                    std::uint32_t device_index = K_HOST_DEVICE_INDEX;

                    /** @brief Profiler thread index of the thread that submitted the dispatch; 0 when unknown. */
                    std::uint32_t submit_thread = 0;
                };

                /**
                 * @brief Holds one worker's in-flight events and never drops one.
                 * @see modules/engine/scheduler/README.md
                 */
                struct ActiveEventList
                {
                    /** @brief Inline capacity before any heap allocation is needed. */
                    static constexpr size_t K_INLINE_CAP = 256;

                    PolledEvent inline_events[K_INLINE_CAP];   /**< Inline fast-path storage. */
                    size_t inline_count = 0;                 /**< Entries held inline (≤ K_INLINE_CAP). */
                    std::vector<PolledEvent> spill;          /**< Overflow storage, used only past K_INLINE_CAP. */

                    // Backoff and stall state belong to the one worker that owns this list.
                    /** @brief Counter for adaptive backoff strategy. */
                    size_t backoff_counter = 0;
                    /** @brief Consecutive no-progress polls, used to drive lazy events. */
                    size_t stall_polls_ = 0;

                    /** @brief Total number of tracked events, inline plus spilled. */
                    size_t size() const { return inline_count + spill.size(); }

                    bool is_empty() const { return inline_count == 0 && spill.empty(); }

                    /**
                     * @brief Indexes the list as one contiguous sequence.
                     *
                     * Indices [0, inline_count) address the inline buffer; the rest
                     * address the spill vector in order.
                     * @param i Logical index in [0, size()).
                     * @return Reference to the addressed entry.
                     */
                    PolledEvent& operator[](size_t i)
                    {
                        return i < inline_count ? inline_events[i]
                                                : spill[i - inline_count];
                    }

                    /**
                     * @brief Records an in-flight event. Never fails.
                     *
                     * Fills the inline buffer first, then spills to the heap. It has no
                     * full or overflow path and never drops an event.
                     * @param pe The event/task pair to track.
                     */
                    void push(const PolledEvent& pe)
                    {
                        if (inline_count < K_INLINE_CAP)
                            inline_events[inline_count++] = pe;
                        else
                            spill.push_back(pe);
                    }

                    /**
                     * @brief Removes entry @p index in O(1) by moving the tail into it.
                     *
                     * The tail is the last spill entry when the spill vector is not empty,
                     * and the last inline entry otherwise.
                     * @param index Logical index to remove; out-of-range is a no-op.
                     */
                    void remove_at(size_t index)
                    {
                        const size_t n = size();
                        if (index >= n) return;

                        const size_t last = n - 1;
                        if (index != last)
                            (*this)[index] = std::move((*this)[last]);

                        if (!spill.empty())
                            spill.pop_back();
                        else
                            --inline_count;
                    }
                };

                /**
                 * @brief Names the coordinator told of every executed graph node this poller reaps.
                 * @pre No worker is polling.
                 * @param offload Not owned; null for none.
                 */
                void set_offload_coordinator(Internal::Graph::OffloadCoordinator* offload) noexcept
                {
                    offload_ = offload;
                }

                /**
                 * @brief Turns per-node kernel timing capture on or off.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_profiling(bool on) noexcept { profiling_.store(on, std::memory_order_release); }

                /**
                 * @brief Wires the plan's event ledger for the round about to run.
                 * @param ledger The plan's ledger, or null when it does not profile.
                 */
                void set_ledger(Internal::Observability::ProfilingLedger* ledger) noexcept
                {
                    ledger_.store(ledger, std::memory_order_release);
                }

                /**
                 * @brief Wires the scheduler's run-quiescence latch into the poller.
                 * @param latch Owned by the scheduler and outlives the poller; null unwires it.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_inflight_tracker(QuiescenceLatch* latch) noexcept
                {
                    quiescence_ = latch;
                }

            private:
                /**
                 * @brief Turns an entry whose query or wait threw into what a skipped task is:
                 *        ready, eventless and not executed.
                 * @return True, the value the entry's done flag takes.
                 */
                static bool reap_failed(PolledEvent& entry)
                {
                    SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "Polling the event of task '{}' threw; the task is reaped "
                                 "as not executed and its successors are released.",
                                 entry.task ? entry.task->name() : "<none>");
                    entry.ready = true;
                    entry.has_event = false;
                    entry.executed = false;
                    return true;
                }

                /**
                 * @brief Records one newly-ready task that will later be reaped here.
                 *
                 * Paired one-to-one with drop_inflight(): every task that reaches
                 * poll_events' release did so via submit() or a successor push, and is
                 * counted exactly once.
                 */
                void add_inflight() noexcept
                {
                    if (quiescence_ != nullptr)
                        quiescence_->add();
                }

                /**
                 * @brief Accounts for one task leaving the run and wakes the waiter on the last
                 *        one.
                 * @see modules/engine/scheduler/README.md
                 */
                void drop_inflight() noexcept
                {
                    if (quiescence_ != nullptr)
                        quiescence_->retire();
                }

                /** @brief Run-quiescence latch, owned by the scheduler; null when none is wired. */
                QuiescenceLatch* quiescence_ = nullptr;

                /** @brief Told of each locally completed graph node; null for none. */
                Internal::Graph::OffloadCoordinator* offload_ = nullptr;
                /** @brief Whether to accumulate per-node kernel timing on completion. */
                std::atomic<bool> profiling_{false};
                /** @brief The armed round's event ledger, or null when it does not profile. */
                std::atomic<Internal::Observability::ProfilingLedger*> ledger_{nullptr};
                /** @brief Counts polls that waited on their oldest event, across workers. */
                std::atomic<std::uint64_t> stall_drives_{0};
                /** @brief Maximum value for the backoff counter. */
                static constexpr size_t K_MAX_BACKOFF = 64;
                /** @brief No-progress polls before forcing a blocking wait. */
                static constexpr size_t K_STALL_DRIVE_THRESHOLD = 32;

            public:
                /**
                 * @brief Keeps @p first when it holds a failure and logs @p next as the one dropped;
                 *        otherwise stores @p next in it.
                 */
                static void keep_first_failure(std::optional<Error>& first, Error next)
                {
                    if (!first)
                    {
                        first = std::move(next);
                        return;
                    }
                    SR_LOG_WARN(K_SCHEDULER_LOG_CATEGORY, "A second event failed in the same poll: {}; the poll "
                                "returns the first failure, '{}'.", next.message, first->message);
                }

                /** @brief Returns how many stalled polls waited on their oldest event since the last take. */
                [[nodiscard]] std::uint64_t stall_drives() const noexcept
                {
                    return stall_drives_.load(std::memory_order_relaxed);
                }

                /** @brief Returns the stalled-poll count and starts it again from zero. */
                std::uint64_t take_stall_drives() noexcept
                {
                    return stall_drives_.exchange(0, std::memory_order_relaxed);
                }

                /**
                 * @brief Checks for completed events and triggers successors.
                 *
                 * Reaps an entry whose status query or wait throws as a task that did not
                 * execute, and returns the first such failure once the pass is over.
                 * @return The error of the first query or wait that threw, or nothing when none did.
                 */
                template<typename QueueType>
                [[nodiscard]] std::optional<Error> poll_events(ActiveEventList& active_events,
                                                               QueueType& local_queue)
                {
                    if (active_events.is_empty())
                        return std::nullopt;

                    // Backoff and stall state are read from the caller's own ActiveEventList.
                    size_t& backoff_counter = active_events.backoff_counter;
                    size_t& stall_polls_ = active_events.stall_polls_;

                    // skip checks if backoff is active to reduce overhead
                    if (backoff_counter > 0)
                    {
                        if (backoff_counter > 16)
                            std::this_thread::yield();

                        backoff_counter--;
                        return std::nullopt;
                    }

                    const bool profiling = profiling_.load(std::memory_order_acquire);

                    bool progress = false;
                    std::optional<Error> first_failure;
                    for (size_t i = 0; i < active_events.size(); )
                    {
                        PolledEvent& entry = active_events[i];
                        bool done = entry.ready;
                        Result<void> polled = SYCL::translate_exceptions([&entry, &done]()
                        {
                            if (!done)
                            {
                                auto status = entry.event.get_info<sycl::info::event::command_execution_status>();
                                done = (status == sycl::info::event_command_status::complete);
                            }

                            // wait() on a complete event makes the runtime flush USM visibility.
                            if (done && entry.has_event)
                                entry.event.wait();
                        });
                        if (!polled)
                        {
                            keep_first_failure(first_failure, std::move(polled.error()));
                            done = reap_failed(entry);
                        }

                        if (done)
                        {
                            // Every node completes here, so profiling adds once per dispatch.
                            if (profiling)
                                accumulate_profiling(entry);

                            // Told before successors are released, and only of a task that ran.
                            if (offload_ != nullptr && entry.executed)
                                report_local_completion(entry.task);

                            trigger_successors(entry.task, local_queue);

                            if (entry.task)
                                entry.task->release();

                            // Dropped after successors are counted; the count never dips to zero.
                            drop_inflight();

                            // remove_at() moves the last entry into slot i, so i does not advance.
                            active_events.remove_at(i);
                            progress = true;
                        }
                        else
                            ++i;
                    }

                    // adjust backoff based on progress
                    if (progress)
                    {
                        backoff_counter = 0;
                        stall_polls_ = 0;
                    }
                    else
                    {
                        backoff_counter = std::min(backoff_counter + 2, K_MAX_BACKOFF);

                        // After K_STALL_DRIVE_THRESHOLD idle polls the oldest event is waited on.
                        if (++stall_polls_ >= K_STALL_DRIVE_THRESHOLD && !active_events.is_empty())
                        {
                            stall_drives_.fetch_add(1, std::memory_order_relaxed);
                            auto& oldest = active_events[0];
                            if (oldest.has_event)
                            {
                                Result<void> waited = SYCL::translate_exceptions(
                                    [&oldest]() { oldest.event.wait(); });
                                if (!waited)
                                {
                                    keep_first_failure(first_failure, std::move(waited.error()));
                                    reap_failed(oldest);
                                }
                            }
                            oldest.ready = true;
                            stall_polls_ = 0;
                            backoff_counter = 0;
                        }
                    }

                    // Every failed entry was reaped above, so no later poll meets it again.
                    return first_failure;
                }

            private:
                /**
                 * @brief Tells the installed coordinator that the pool finished a graph node.
                 * @pre offload_ is not null.
                 * @param task The reaped task; null or a non-graph task is not reported.
                 */
                void report_local_completion(SushiRuntime::Internal::Scheduler::Task* task) noexcept
                {
                    if (task && task->is_graph_node())
                        offload_->on_local_completion(*static_cast<Internal::Graph::Node*>(task));
                }

                /**
                 * @brief Writes this dispatch's event record before the completion is published.
                 *
                 * A dispatch that leaves no record is counted in the ledger under its cause.
                 * @see modules/engine/scheduler/README.md
                 */
                void accumulate_profiling(const PolledEvent& pe) noexcept
                {
                    if (!pe.task)
                        return;

                    Internal::Observability::ProfilingLedger* ledger =
                        ledger_.load(std::memory_order_acquire);
                    if (ledger == nullptr)
                        return;

                    auto* node = pe.task->is_graph_node()
                        ? static_cast<SushiRuntime::Internal::Graph::Node*>(pe.task) : nullptr;

                    Internal::Observability::EventTimestamps times;
                    const bool have_start_end = pe.has_event && Internal::Observability::read_dispatch_timestamps(pe.event, pe.handed_over, times);
                    if (!have_start_end)
                    {
                        // A task that did not run, a sentinel and a native host node leave no event by design.
                        if (node != nullptr && pe.executed && !node->is_sentinel)
                        {
                            if (pe.has_event)
                                ledger->note_drop(DropCause::UnreadableEvent,
                                                  node->registration_index);
                            else if (node->execution_mode() == SushiRuntime::Internal::Scheduler::Task::ExecutionMode::Kernel)
                                ledger->note_drop(DropCause::NoEvent,
                                                  node->registration_index);
                        }
                        return;
                    }

                    if (times.has_submit && ledger->is_pairing_owner(pe.device_index, pe.task))
                        ledger->set_pairing_event_ns(pe.device_index, times.submit_ns);

                    if (node == nullptr)
                        return;

                    Internal::Observability::DispatchFacts facts;
                    facts.node_index = node->registration_index;
                    facts.invocation = pe.step;
                    facts.launched_width = pe.widths.launched;
                    facts.live_width = pe.widths.live;
                    facts.device_index = pe.device_index;
                    facts.submit_thread = pe.submit_thread;
                    ledger->record(facts, Internal::Observability::DispatchTimes{times.submit_ns, times.start_ns, times.end_ns});
                }

                /**
                 * @brief Makes runnable every successor a finished task freed.
                 * @param queue Current thread's task queue to push new ready nodes.
                 * @see modules/engine/scheduler/README.md
                 */
                template<typename QueueType>
                void trigger_successors(SushiRuntime::Internal::Scheduler::Task* task, QueueType& queue)
                {
                    if (!task)
                    {
                        return;
                    }

                    static thread_local SushiRuntime::Internal::Scheduler::ReadySink freed;
                    freed.clear();
                    task->on_complete(freed);

                    for (SushiRuntime::Internal::Scheduler::Task* succ : freed)
                    {
                        succ->add_ref();
                        add_inflight();
                        queue.push(succ);
                    }
                }
        };
        
    } // namespace Scheduler
} // namespace SushiRuntime
