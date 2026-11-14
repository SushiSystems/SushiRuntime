/**************************************************************************/
/* spsc_ring.hpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file spsc_ring.hpp
 * @brief Declares SPSCRing, the bounded lock-free ring one thread pushes into and one thread pops.
 * @author Mustafa Garip
 * @see modules/engine/work_stealing/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/core/common.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Implements a bounded single-producer, single-consumer ring over two counters.
         * @tparam Value Default-constructible and move-assignable.
         * @tparam Capacity A power of two of at least 2.
         * @see modules/engine/work_stealing/README.md
         */
        template <class Value, std::size_t Capacity>
        class SPSCRing final
        {
            static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                          "SPSCRing: the capacity must be a power of two of at least 2.");

            public:
                /** @brief The number of values the ring holds at most. */
                static constexpr std::size_t K_CAPACITY = Capacity;

                /** @brief Builds an empty ring. */
                SPSCRing() : slots_(std::make_unique<Value[]>(Capacity))
                {

                }

                SPSCRing(const SPSCRing&) = delete;
                SPSCRing& operator=(const SPSCRing&) = delete;
                SPSCRing(SPSCRing&&) = delete;
                SPSCRing& operator=(SPSCRing&&) = delete;
                ~SPSCRing() = default;

                /**
                 * @brief Pushes @p value at the tail; only the one producer thread may call it.
                 * @param value Moved from on success, left as it was on failure.
                 * @return False when the ring is full; nothing is changed then.
                 */
                [[nodiscard]] bool try_push(Value& value)
                {
                    const std::size_t head = head_.load(std::memory_order_relaxed);
                    if (head - tail_.load(std::memory_order_acquire) == Capacity)
                        return false;
                    slots_[head & K_MASK] = std::move(value);
                    head_.store(head + 1, std::memory_order_release);
                    return true;
                }

                /**
                 * @brief Pops the oldest value; only the one consumer thread may call it.
                 * @param out Receives the value on success; untouched otherwise.
                 * @return False when the ring is empty.
                 */
                [[nodiscard]] bool try_pop(Value& out)
                {
                    const std::size_t tail = tail_.load(std::memory_order_relaxed);
                    if (tail == head_.load(std::memory_order_acquire))
                        return false;
                    out = std::move(slots_[tail & K_MASK]);
                    tail_.store(tail + 1, std::memory_order_release);
                    return true;
                }

            private:
                /** @brief Masks a position into a slot index. */
                static constexpr std::size_t K_MASK = Capacity - 1;

                /// @brief The slots, indexed by position modulo the capacity.
                std::unique_ptr<Value[]> slots_;
                /// @brief The next position the producer writes; written by the producer alone.
                alignas(SushiRuntime::K_CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
                /// @brief The next position the consumer reads; written by the consumer alone.
                alignas(SushiRuntime::K_CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
