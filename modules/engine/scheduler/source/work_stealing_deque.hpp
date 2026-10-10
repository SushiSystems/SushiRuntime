/**************************************************************************/
/* work_stealing_deque.hpp                                                */
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
 * @file work_stealing_deque.hpp
 * @brief Declares Deque, the lock-free Chase-Lev work-stealing deque.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/core/ref_counted.hpp>

namespace SushiRuntime
{
    namespace Scheduler
    {
        /**
         * @brief Implements a lock-free Chase-Lev work-stealing deque of fixed capacity.
         * @see modules/engine/scheduler/README.md
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
                // Slots stay uninitialised; push() writes each one before any consumer reads it.
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
                    // A full ring parks the surplus in overflow_; pop() drains it back.
                    overflow_.push_back(task);
                    return;
                }

                items[b & MASK].store(task, std::memory_order_relaxed);

                // The release store on bottom publishes the slot write.
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
                // A relaxed pre-check may fail spuriously but never returns a stale task.
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
                    // The slot is released through the bottom store, as in push().
                    bottom.store(++b, std::memory_order_release);

                    // top is re-read so a burst of steals does not end the drain early.
                    t = top.load(std::memory_order_acquire);
                }
            }
        };

    } // namespace Scheduler
} // namespace SushiRuntime
