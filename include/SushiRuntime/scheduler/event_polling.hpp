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
#include <thread>
#include <utility>
#include <vector>
#include <functional>
#include <sycl/sycl.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/logger.hpp>

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
                 * profiling choice. When off, the completion path reads no event
                 * timestamps and touches no node counters. Read-only during a run, so
                 * a plain bool is safe across the workers that poll.
                 *
                 * @param on True to accumulate device time and invocation counts.
                 */
                void set_profiling(bool on) noexcept { profiling_ = on; }

            private:
#ifdef SR_DISTRIBUTED
                /** @brief Optional per-completion observer (Master mode only). */
                std::function<void(SushiRuntime::Scheduler::Task*)> on_node_complete_;
#endif
                /** @brief Whether to accumulate per-node kernel timing on completion. */
                bool profiling_ = false;
                /** @brief Counter for adaptive backoff strategy. */
                size_t backoff_counter = 0;
                /** @brief Maximum value for the backoff counter. */
                static constexpr size_t MAX_BACKOFF = 64;
                /** @brief Consecutive no-progress polls, used to drive lazy events. */
                size_t stall_polls_ = 0;
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

                    // skip checks if backoff is active to reduce overhead
                    if (backoff_counter > 0) 
                    {
                        if (backoff_counter > 16)
                            std::this_thread::yield();

                        backoff_counter--;
                        return;
                    }

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
                            if (active_events[i].event != sycl::event{})
                            {
                                active_events[i].event.wait();
                            }

                            // The completion chokepoint every node passes through, so
                            // the per-node invocation count and kernel device time are
                            // accumulated here exactly once per dispatch. No-op unless
                            // the run was armed for profiling.
                            if (profiling_)
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
                            if (oldest.event != sycl::event{})
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
                 * @brief Accumulates a finished task's timing onto its node.
                 *
                 * Counts one invocation for every completed graph node (the sentinel
                 * excepted, as it is plumbing, not a system) and adds the kernel's
                 * device time when the event carries profiling info. A non-profiled or
                 * inline-ready event has no device timing, so only the count is bumped;
                 * a backend that cannot report profiling is tolerated, not fatal.
                 *
                 * @param pe The completed event/task pair being reaped.
                 */
                void accumulate_profiling(const PolledEvent& pe) noexcept
                {
                    if (!pe.task || !pe.task->is_graph_node())
                        return;
                    auto* node = static_cast<SushiRuntime::Graph::Node*>(pe.task);
                    if (node->is_sentinel)
                        return;

                    node->profiling.invocations.fetch_add(1, std::memory_order_relaxed);

                    if (pe.event == sycl::event{})
                        return;
                    try
                    {
                        const auto start = pe.event.get_profiling_info<
                            sycl::info::event_profiling::command_start>();
                        const auto end = pe.event.get_profiling_info<
                            sycl::info::event_profiling::command_end>();
                        if (end > start)
                            node->profiling.device_ns.fetch_add(end - start,
                                                                std::memory_order_relaxed);
                    }
                    catch (...)
                    {
                        // Backend without profiling support: keep the invocation count,
                        // skip device time. Not an error.
                    }
                }

                /**
                 * @brief Activates next nodes in the graph when a task finishes.
                 * @param task Finished task pointer.
                 * @param queue Current thread's task queue to push new ready nodes.
                 */
                template<typename QueueType>
                void trigger_successors(SushiRuntime::Scheduler::Task* task, QueueType& queue) 
                {
                    if (!task || !task->is_graph_node())
                    {
                        return;
                    }

                    auto* node = static_cast<SushiRuntime::Graph::Node*>(task);
                    
                    int32_t count = node->inline_successor_count.load(std::memory_order_acquire);
                    int32_t max_idx = std::min(count, SushiRuntime::Graph::Node::MAX_INLINE_SUCCESSORS);
                    
                    // iterate through inline successors
                    for (int32_t i = 0; i < max_idx; ++i)
                    {
                        auto* succ = node->inline_successors[i];
                        if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1) 
                        {
                            succ->add_ref();
                            queue.push(succ);
                        }
                    }

                    // iterate through spillover successors in a lock-free way
                    auto* curr = node->successor_list.load(std::memory_order_acquire);
                    while(curr)
                    {
                        auto* succ = curr->target;

                        // if all dependencies are met, push the node to the queue
                        if(succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        {
                            succ->add_ref();
                            queue.push(succ);
                        }

                        curr = curr->next;
                    }

                    // Cross-region (boundary) successors of the dynamic region graph.
                    // This vector is mutated only at a step boundary, with no worker
                    // running, and is read-only here, so it needs no atomics of its
                    // own; the per-target in_degree decrement carries the same
                    // acq_rel ordering as the intra successors above. Empty (and so
                    // skipped) for every static-graph node and every dynamic node
                    // that exports nothing across a boundary.
                    for (auto* succ : node->region.boundary_successors)
                    {
                        if (succ && succ->in_degree.fetch_sub(1, std::memory_order_acq_rel) == 1)
                        {
                            succ->add_ref();
                            queue.push(succ);
                        }
                    }
                }
        };
        
    } // namespace Scheduler
} // namespace SushiRuntime
