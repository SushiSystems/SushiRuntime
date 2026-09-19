/**************************************************************************/
/* event_polling.hpp                                                      */
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

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <functional>
#include <sycl/sycl.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/scheduler/ready_sink.hpp>
#include <SushiRuntime/core/event_timestamps.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/profiling_ledger.hpp>
#include <SushiRuntime/graph/node.hpp>

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
                     * @brief Already-complete marker for synchronous host work.
                     *
                     * A host task whose execute_host() runs inline returns an empty
                     * event — there is nothing to wait on. We still route it through
                     * the poller (to trigger successors and release uniformly), but
                     * flag it ready so completion is not gated on a SYCL event status
                     * query. This also sidesteps toolchains (AdaptiveCpp) whose host
                     * operations are not observable via command_execution_status.
                     */
                    bool ready = false;

                    /**
                     * @brief Whether `event` is a real SYCL event (vs. an empty
                     *        default one for inline host work).
                     *
                     * Cached at push time so the poll loop can decide whether to
                     * wait()/query without constructing a throwaway `sycl::event{}`
                     * on every iteration — that default construction can heap-
                     * allocate in DPC++, and the comparison runs per event per poll.
                     */
                    bool has_event = false;

                    /** @brief Run step this dispatch belongs to, captured by the submitting worker. */
                    std::uint32_t step = 0;
                };

                /**
                 * @brief In-flight events for one worker; never drops an event.
                 *
                 * The common case (≤ INLINE_CAP outstanding events) stays in an
                 * inline buffer with no allocation. Beyond that the list spills to a
                 * heap vector — the same inline-then-spill shape as SmallEventVector.
                 * Dropping an event would orphan its successors (their in_degree
                 * never reaches zero) and deadlock the run, so push() must always
                 * store, even if that means growing.
                 */
                struct ActiveEventList
                {
                    /** @brief Inline capacity before any heap allocation is needed. */
                    static constexpr size_t INLINE_CAP = 256;

                    PolledEvent inline_events[INLINE_CAP];   /**< Inline fast-path storage. */
                    size_t inline_count = 0;                 /**< Entries held inline (≤ INLINE_CAP). */
                    std::vector<PolledEvent> spill;          /**< Overflow storage, used only past INLINE_CAP. */

                    // Backoff/stall state is intrinsically per-worker: it tracks the
                    // polling cadence of the one thread that owns this list. It lives
                    // here (not on the shared EventPolling instance) so concurrent
                    // pollers cannot race on it — an interleaved decrement could wrap
                    // a counter to ~2^64 and wedge every future poll into the
                    // early-return branch, stranding in-flight events and hanging the
                    // run.
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

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Installs an observer fired for each completed task.
                 *
                 * Used in Master mode to bump the versions of any registered
                 * buffers the completed node wrote, keeping remote caches coherent.
                 */
                void set_completion_observer(std::function<void(SushiRuntime::Scheduler::Task*)> cb)
                {
                    on_node_complete_ = std::move(cb);
                }
#endif

                /**
                 * @brief Turns per-node kernel timing capture on or off.
                 *
                 * Set once by the scheduler when it arms a run, from the context's
                 * profiling choice. The re-arm at the end of a run happens on a
                 * worker thread (the sentinel's host_work), concurrently with other
                 * workers polling, so this is an atomic release/acquire rather than a
                 * plain bool.
                 *
                 * @param on True to accumulate device time and invocation counts.
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
                 *
                 * The poller is the single chokepoint where a task's scheduling life
                 * ends (release on completion) and where new ready successors are
                 * born (trigger_successors). It therefore owns the increment/decrement
                 * of the in-flight counter the scheduler blocks on to know a run has
                 * fully drained — see TaskScheduler::wait_quiescent(). Null tracker
                 * (never wired) makes every hook a no-op.
                 *
                 * @param counter In-flight task counter owned by the scheduler.
                 * @param mutex   Guards the wait predicate against a lost wakeup.
                 * @param cv      Signalled when the counter reaches zero.
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
                 * @brief Accounts for one task leaving the run and wakes the waiter on
                 *        the last one.
                 *
                 * The brief lock closes the classic lost-wakeup window: a waiter that
                 * reads a non-zero count just before this store would otherwise sleep
                 * past the notify. Locking the same mutex it waits under orders the
                 * store ahead of its predicate re-check.
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

#ifdef SR_DISTRIBUTED
                /** @brief Optional per-completion observer (Master mode only). */
                std::function<void(SushiRuntime::Scheduler::Task*)> on_node_complete_;
#endif
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

                    // Backoff/stall state is per-worker: read it from the caller's
                    // own ActiveEventList, not from this shared instance.
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
                            // In AdaptiveCpp (and some other SYCL implementations), get_info<complete>
                            // might return true before implicit USM data migrations are fully visible.
                            // Since we already know it's complete, calling wait() is instantaneous
                            // but guarantees the SYCL runtime flushes memory visibility.
                            if (active_events[i].has_event)
                            {
                                active_events[i].event.wait();
                            }

                            // The completion chokepoint every node passes through, so
                            // the per-node invocation count and kernel device time are
                            // accumulated here exactly once per dispatch. No-op unless
                            // the run was armed for profiling.
                            if (profiling)
                                accumulate_profiling(active_events[i]);

#ifdef SR_DISTRIBUTED
                            // Observe completion before releasing successors, so a
                            // write's version bump is visible to any dependent task
                            // (e.g. an offloaded reader) by the time it is dispatched.
                            if (on_node_complete_)
                                on_node_complete_(active_events[i].task);
#endif
                            trigger_successors(active_events[i].task, local_queue);

                            if (active_events[i].task)
                                active_events[i].task->release();

                            // This task's scheduling life ends here: account for it
                            // after its successors are born (add_inflight in
                            // trigger_successors) so the count never dips to zero
                            // mid-graph and only reaches zero once the terminal
                            // sentinel is reaped.
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

                        // Some SYCL runtimes (AdaptiveCpp) use a lazy event model:
                        // command_execution_status never advances to complete via
                        // non-blocking polling — only an explicit wait() drives the
                        // DAG. After a few consecutive no-progress polls, block on the
                        // oldest pending event to force it forward and mark it ready so
                        // it is reaped on the next pass regardless of how the runtime
                        // reports status. On runtimes whose status advances on its own
                        // (Intel), polling completes before the threshold, so this
                        // rarely fires and the hot path is unchanged.
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
                 * @brief Writes this dispatch's event record before the completion is published.
                 *
                 * Called from poll_events() before trigger_successors() and drop_inflight():
                 * the latter's zero transition is what wakes a waiting complete() into
                 * collect_profiling() (or a next reserve()), so every ledger read and
                 * write must finish here first, not after either of those calls.
                 *
                 * @param pe The completed event/task pair being reaped.
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
                 * @brief Activates next nodes in the graph when a task finishes.
                 *
                 * Asks the task which of its dependents this completion freed, then
                 * makes each one runnable: take the scheduling reference the queue
                 * will own, count it in flight, and land it. The task answers the
                 * first question because only it knows how its dependents are stored;
                 * the second half stays here because only the scheduler knows the
                 * queue. Neither has to name the other's type.
                 *
                 * The buffer is thread-local and reused, so a steady-state step does
                 * no allocation, and pushing stays inlined through the concrete queue
                 * — a virtual call per freed successor measured 4.6x slower on the
                 * wide fan-out benchmark.
                 *
                 * @param task Finished task pointer.
                 * @param queue Current thread's task queue to push new ready nodes.
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
