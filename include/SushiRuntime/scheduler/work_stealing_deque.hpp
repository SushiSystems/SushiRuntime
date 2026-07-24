/**************************************************************************/
/* work_stealing_deque.hpp                                                */
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

#include <atomic>
#include <cstdint>
#include <vector>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/scheduler/task.hpp>
#include <SushiRuntime/core/ref_counted.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Lock-free Chase-Lev work-stealing deque for task scheduling.
         *
         * A fixed-capacity power-of-2 ring buffer avoids dynamic resizing and the
         * memory-reclamation hazards a growable lock-free buffer would carry. The
         * owner thread pushes and pops the bottom (LIFO); stealer threads steal
         * from the top (FIFO). Each slot is a std::atomic<Task*> so the array is
         * accessed atomically.
         *
         * Overflow never drops. push() is single-producer (owner-only), so when the
         * ring is full the surplus goes into an owner-private overflow buffer that
         * needs no synchronization; pop() drains it back into the ring as space
         * frees up. A dropped task would leave its successors' in_degree above zero
         * forever and deadlock the run, so dropping is not an option. Overflowed
         * tasks are not visible to stealers until drained, which is acceptable: the
         * owner always makes progress on them, and overflow is a rare pathology.
         */
        class alignas(SushiRuntime::Core::CACHE_LINE_SIZE) Deque : public Core::RefCounted
        {
        public:
            /** @brief Fixed ring capacity. 256K is huge for DAG workloads. */
            static constexpr int64_t CAPACITY = 262144;
            static constexpr int64_t MASK = CAPACITY - 1;

            /** @brief Ring buffer of atomic slots (C++17 replacement for atomic_ref). */
            std::atomic<Task*>* items;

            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> top;
            alignas(SushiRuntime::Core::CACHE_LINE_SIZE) std::atomic<int64_t> bottom;

            /**
             * @brief Owner-private surplus, used only when the ring is full.
             *
             * Touched solely by the owner thread (push/pop), so it carries no
             * atomics. Holds tasks LIFO until pop() can move them back into the ring.
             */
            std::vector<Task*> overflow_;

        public:
            Deque() : top(0), bottom(0)
            {
                // Default-initialize (no trailing "()"): a slot is always written by
                // push() before any consumer can read it (push stores the slot, then
                // publishes via the release store on bottom; steal()/pop() only read
                // items[i] for i < bottom), so zeroing all CAPACITY slots up front is
                // wasted work — it faults in the whole 2 MB ring eagerly. Leaving them
                // indeterminate lets the pages fault lazily as the deque actually grows.
                items = new std::atomic<Task*>[CAPACITY];
            }

            ~Deque() override
            {
                delete[] items;
            }

            /** @brief Ring occupancy (excludes the owner-private overflow). */
            inline size_t size() const
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_relaxed);

                return (b >= t) ? static_cast<size_t>(b - t) : 0;
            }

            /** @brief Number of tasks parked in the owner-private overflow buffer. */
            inline size_t overflow_size() const { return overflow_.size(); }

            /**
             * @brief Pushes a task onto the bottom (owner thread only).
             *
             * Stores into the ring when there is room; otherwise parks the task in
             * the owner-private overflow buffer so nothing is ever dropped.
             * @param task Pointer to the task to push.
             */
            void push(Task* task)
            {
                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);

                if (b - t >= CAPACITY)
                {
                    // Ring is full. Park the surplus privately rather than drop it;
                    // pop() drains it back in as the ring empties.
                    overflow_.push_back(task);
                    return;
                }

                items[b & MASK].store(task, std::memory_order_relaxed);

                // Publish the slot write by releasing it through the bottom store
                // rather than a standalone release fence. The two are equivalent for
                // correctness — a stealer's acquire load of bottom orders the slot
                // write before its own read — but a release store on the very
                // variable the consumer acquires is synchronization ThreadSanitizer
                // models precisely, whereas it does not fully track standalone
                // atomic_thread_fence and falsely flags the published task as racy.
                bottom.store(b + 1, std::memory_order_release);
            }

            /**
             * @brief Removes and returns a task from the bottom (owner thread).
             *
             * First tops the ring back up from the overflow buffer (if any), so an
             * overflowed task is never stranded, then performs the standard
             * Chase-Lev bottom pop.
             * @return Pointer to the popped task, or nullptr if empty.
             */
            Task* pop()
            {
                refill_from_overflow();

                int64_t b = bottom.load(std::memory_order_relaxed) - 1;
                bottom.store(b, std::memory_order_relaxed);

                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t t = top.load(std::memory_order_relaxed);

                if (t <= b)
                {
                    Task* task = items[b & MASK].load(std::memory_order_relaxed);

                    if (t == b)
                    {
                        if (!top.compare_exchange_strong(t,
                                                         t + 1,
                                                         std::memory_order_seq_cst,
                                                         std::memory_order_relaxed))
                        {
                            task = nullptr;
                        }
                        bottom.store(b + 1, std::memory_order_relaxed);
                    }
                    return task;
                }

                bottom.store(b + 1, std::memory_order_relaxed);
                return nullptr;
            }

            /**
             * @brief Steals a task from the top (stealer threads).
             * @return Pointer to the stolen task, or nullptr if empty.
             */
            Task* steal()
            {
                // Relaxed emptiness pre-check. A stealer scanning idle peers — and
                // every worker at end-of-step quiescence — hits empty victims far
                // more often than not, and the full protocol below pays a seq_cst
                // fence (an MFENCE on x86) on every attempt. Skip it when the ring is
                // obviously empty. This can only ever make steal() fail spuriously,
                // which the caller already tolerates (it retries / scans other
                // victims and the owner still makes progress); it never returns a
                // stale task, because the fenced protocol below re-reads and
                // re-validates via the top CAS before returning anything.
                if (top.load(std::memory_order_relaxed) >=
                    bottom.load(std::memory_order_relaxed))
                    return nullptr;

                int64_t t = top.load(std::memory_order_acquire);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                int64_t b = bottom.load(std::memory_order_acquire);

                if (t < b)
                {
                    Task* task = items[t & MASK].load(std::memory_order_relaxed);

                    if (!top.compare_exchange_strong(t,
                                                     t + 1,
                                                     std::memory_order_seq_cst,
                                                     std::memory_order_relaxed))
                    {
                        return nullptr;
                    }

                    return task;
                }
                return nullptr;
            }

        private:
            /**
             * @brief Moves parked overflow tasks back into the ring while it has room.
             *
             * Owner-only, so the overflow vector needs no locking. Each moved task
             * follows the same release-fence-then-publish-bottom protocol as push(),
             * so a concurrent stealer observes a fully-written slot.
             */
            void refill_from_overflow()
            {
                if (overflow_.empty()) return;

                int64_t b = bottom.load(std::memory_order_relaxed);
                int64_t t = top.load(std::memory_order_acquire);

                while (!overflow_.empty() && (b - t) < CAPACITY)
                {
                    Task* task = overflow_.back();
                    overflow_.pop_back();

                    items[b & MASK].store(task, std::memory_order_relaxed);
                    // Release through the bottom store, as in push() — see the note
                    // there on why this replaces a standalone release fence.
                    bottom.store(++b, std::memory_order_release);

                    // Refresh the stolen-from end so a burst of steals lets us keep
                    // draining rather than bailing on a stale full reading.
                    t = top.load(std::memory_order_acquire);
                }
            }
        };

    } // namespace Scheduler
} // namespace SushiRuntime
