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
#include <thread>
#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/ready_sink.hpp>
#include <SushiRuntime/observability/event_timestamps.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/observability/profiling_ledger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        using namespace SushiRuntime::Core;

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
                    SushiRuntime::Scheduler::Task* task = nullptr;
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
                     * @brief Tells whether the task's work ran; false for a task released unrun.
                     * @see modules/engine/scheduler/README.md
                     */
                    bool executed = true;

                    /** @brief Run step this dispatch belongs to, captured by the submitting worker. */
                    std::uint32_t step = 0;

                    /** @brief The dispatched node's widths, snapshotted where the event is armed. */
                    SushiRuntime::Graph::NodeWidths widths{};
                };

                /**
                 * @brief Holds one worker's in-flight events and never drops one.
                 * @see modules/engine/scheduler/README.md
                 */
                struct ActiveEventList
                {
                    /** @brief Inline capacity before any heap allocation is needed. */
                    static constexpr size_t INLINE_CAP = 256;

                    PolledEvent inline_events[INLINE_CAP];   /**< Inline fast-path storage. */
                    size_t inline_count = 0;                 /**< Entries held inline (≤ INLINE_CAP). */
                    std::vector<PolledEvent> spill;          /**< Overflow storage, used only past INLINE_CAP. */

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
                     * Fills the inline buffer first, then spills to the heap. There
                     * is deliberately no full/overflow path: a dropped event deadlocks
                     * the run (see the struct note).
                     * @param pe The event/task pair to track.
                     */
                    void push(const PolledEvent& pe)
                    {
                        if (inline_count < INLINE_CAP)
                            inline_events[inline_count++] = pe;
                        else
                            spill.push_back(pe);
                    }

                    /**
                     * @brief Removes entry @p index in O(1) by moving the tail into it.
                     *
                     * Mirrors the original swap-with-last, but the "last" entry may
                     * live in either the inline buffer or the spill vector, so the
                     * tail is dropped from whichever currently holds it.
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
                void set_offload_coordinator(Graph::OffloadCoordinator* offload) noexcept
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
                void set_ledger(Core::ProfilingLedger* ledger) noexcept
                {
                    ledger_.store(ledger, std::memory_order_release);
                }

                /**
                 * @brief Wires the scheduler's run-quiescence tracker into the poller.
                 * @param counter In-flight task counter owned by the scheduler.
                 * @param mutex Guards the wait predicate against a lost wakeup.
                 * @param cv Signalled when the counter reaches zero.
                 * @see modules/engine/scheduler/README.md
                 */
                void set_inflight_tracker(std::atomic<long long>* counter,
                                          std::mutex* mutex,
                                          std::condition_variable* cv) noexcept
                {
                    inflight_ = counter;
                    quiesce_mutex_ = mutex;
                    quiesce_cv_ = cv;
                }

            private:
                /**
                 * @brief Records one newly-ready task that will later be reaped here.
                 *
                 * Paired one-to-one with drop_inflight(): every task that reaches
                 * poll_events' release did so via submit() or a successor push, and is
                 * counted exactly once.
                 */
                void add_inflight() noexcept
                {
                    if (inflight_) inflight_->fetch_add(1, std::memory_order_relaxed);
                }

                /**
                 * @brief Accounts for one task leaving the run and wakes the waiter on the last
                 *        one.
                 * @see modules/engine/scheduler/README.md
                 */
                void drop_inflight() noexcept
                {
                    if (!inflight_) return;
                    if (inflight_->fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        if (quiesce_mutex_ && quiesce_cv_)
                        {
                            { std::lock_guard<std::mutex> lock(*quiesce_mutex_); }
                            quiesce_cv_->notify_all();
                        }
                    }
                }

                /** @brief In-flight run-quiescence tracker (scheduler-owned, may be null). */
                std::atomic<long long>*  inflight_ = nullptr;
                std::mutex*              quiesce_mutex_ = nullptr;
                std::condition_variable* quiesce_cv_ = nullptr;

                /** @brief Told of each locally completed graph node; null for none. */
                Graph::OffloadCoordinator* offload_ = nullptr;
                /** @brief Whether to accumulate per-node kernel timing on completion. */
                std::atomic<bool> profiling_{false};
                /** @brief The armed round's event ledger, or null when it does not profile. */
                std::atomic<Core::ProfilingLedger*> ledger_{nullptr};
                /** @brief Maximum value for the backoff counter. */
                static constexpr size_t MAX_BACKOFF = 64;
                /** @brief No-progress polls before forcing a blocking wait. */
                static constexpr size_t STALL_DRIVE_THRESHOLD = 32;

            public:
                /**
                 * @brief Checks for completed events and triggers successors.
                 *
                 * Uses adaptive backoff to reduce CPU and driver load.
                 *
                 * @param active_events List of events currently being monitored.
                 * @param local_queue Current thread's task queue.
                 */
                template<typename QueueType>
                void poll_events(ActiveEventList& active_events, QueueType& local_queue) 
                {
                    if (active_events.is_empty()) return;

                    // Backoff and stall state are read from the caller's own ActiveEventList.
                    size_t& backoff_counter = active_events.backoff_counter;
                    size_t& stall_polls_ = active_events.stall_polls_;

                    // skip checks if backoff is active to reduce overhead
                    if (backoff_counter > 0)
                    {
                        if (backoff_counter > 16)
                            std::this_thread::yield();

                        backoff_counter--;
                        return;
                    }

                    const bool profiling = profiling_.load(std::memory_order_acquire);

                    bool progress = false;
                    for (size_t i = 0; i < active_events.size(); )
                    {
                        bool done = active_events[i].ready;
                        if (!done)
                        {
                            auto status = active_events[i].event.get_info<sycl::info::event::command_execution_status>();
                            done = (status == sycl::info::event_command_status::complete);
                        }
                        if (done)
                        {
                            // wait() on a complete event makes the runtime flush USM visibility.
                            if (active_events[i].has_event)
                            {
                                active_events[i].event.wait();
                            }

                            // Every node completes here, so profiling adds once per dispatch.
                            if (profiling)
                                accumulate_profiling(active_events[i]);

                            // Told before successors are released, and only of a task that ran.
                            if (offload_ != nullptr && active_events[i].executed)
                                report_local_completion(active_events[i].task);

                            trigger_successors(active_events[i].task, local_queue);

                            if (active_events[i].task)
                                active_events[i].task->release();

                            // Dropped after successors are counted; the count never dips to zero.
                            drop_inflight();

                            active_events.remove_at(i); // O(1): swaps with the last entry
                            progress = true;
                            // Do not increment i — the swapped-in last entry now occupies this slot.
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
                        backoff_counter = std::min(backoff_counter + 2, MAX_BACKOFF);

                        // After STALL_DRIVE_THRESHOLD idle polls the oldest event is waited on.
                        if (++stall_polls_ >= STALL_DRIVE_THRESHOLD && !active_events.is_empty())
                        {
                            auto& oldest = active_events[0];
                            if (oldest.has_event)
                            {
                                try { oldest.event.wait(); }
                                catch (...) { SR_LOG_WARN("Stall-drive wait() threw; treating event as complete."); }
                            }
                            oldest.ready = true;
                            stall_polls_ = 0;
                            backoff_counter = 0;
                        }
                    }
                }

            private:
                /**
                 * @brief Tells the installed coordinator that the pool finished a graph node.
                 * @pre offload_ is not null.
                 * @param task The reaped task; null or a non-graph task is not reported.
                 */
                void report_local_completion(SushiRuntime::Scheduler::Task* task) noexcept
                {
                    if (task && task->is_graph_node())
                        offload_->on_local_completion(*static_cast<Graph::Node*>(task));
                }

                /**
                 * @brief Writes this dispatch's event record before the completion is published.
                 * @see modules/engine/scheduler/README.md
                 */
                void accumulate_profiling(const PolledEvent& pe) noexcept
                {
                    if (!pe.task)
                        return;

                    Core::EventTimestamps times;
                    const bool have_start_end = pe.has_event && Core::read_event_timestamps(pe.event, times);

                    if (have_start_end)
                    {
                        if (Core::ProfilingLedger* ledger = ledger_.load(std::memory_order_acquire))
                        {
                            if (times.has_submit && ledger->is_pairing_owner(pe.task))
                                ledger->set_pairing_event_ns(times.submit_ns);

                            if (pe.task->is_graph_node())
                            {
                                auto* node = static_cast<SushiRuntime::Graph::Node*>(pe.task);
                                const std::uint32_t slot = ledger->claim_slot(node->registration_index);
                                if (slot != Core::ProfilingLedger::npos)
                                {
                                    Core::NodeEventRecord rec{};
                                    rec.node_index = node->registration_index;
                                    rec.invocation = pe.step;
                                    rec.launched_width = pe.widths.launched;
                                    rec.live_width     = pe.widths.live;
                                    rec.submit_ns  = times.submit_ns;
                                    rec.start_ns   = times.start_ns;
                                    rec.end_ns     = times.end_ns;
                                    ledger->write(node->registration_index, slot, rec);
                                }
                            }
                        }
                    }

                }

                /**
                 * @brief Makes runnable every successor a finished task freed.
                 * @param queue Current thread's task queue to push new ready nodes.
                 * @see modules/engine/scheduler/README.md
                 */
                template<typename QueueType>
                void trigger_successors(SushiRuntime::Scheduler::Task* task, QueueType& queue)
                {
                    if (!task)
                    {
                        return;
                    }

                    static thread_local SushiRuntime::Scheduler::ReadySink freed;
                    freed.clear();
                    task->on_complete(freed);

                    for (SushiRuntime::Scheduler::Task* succ : freed)
                    {
                        succ->add_ref();
                        add_inflight();
                        queue.push(succ);
                    }
                }
        };
        
    } // namespace Scheduler
} // namespace SushiRuntime
