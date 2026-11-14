/**************************************************************************/
/* mpsc_ring.hpp                                                          */
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
 * @file mpsc_ring.hpp
 * @brief Declares MPSCRing, the bounded lock-free ring many threads push into and one thread pops.
 * @author Mustafa Garip
 * @see modules/engine/work_stealing/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <SushiRuntime/core/common.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Implements a bounded multi-producer, single-consumer ring with a sequence word per slot.
         * @tparam Value Default-constructible and move-assignable.
         * @tparam Capacity A power of two of at least 2.
         * @see modules/engine/work_stealing/README.md
         */
        template <class Value, std::size_t Capacity>
        class MPSCRing final
        {
            static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                          "MPSCRing: the capacity must be a power of two of at least 2.");

            public:
                /** @brief The number of values the ring holds at most. */
                static constexpr std::size_t K_CAPACITY = Capacity;

                /** @brief Builds an empty ring; every slot waits for the push of its own position. */
                MPSCRing() : slots_(std::make_unique<Slot[]>(Capacity))
                {
                    for (std::size_t i = 0; i < Capacity; ++i)
                        slots_[i].sequence.store(i, std::memory_order_relaxed);
                }

                MPSCRing(const MPSCRing&) = delete;
                MPSCRing& operator=(const MPSCRing&) = delete;
                MPSCRing(MPSCRing&&) = delete;
                MPSCRing& operator=(MPSCRing&&) = delete;
                ~MPSCRing() = default;

                /**
                 * @brief Pushes @p value at the tail; any thread may call it.
                 * @param value Moved from on success, left as it was on failure.
                 * @return False when the ring is full; nothing is changed then.
                 */
                [[nodiscard]] bool try_push(Value& value)
                {
                    std::size_t position = head_.load(std::memory_order_relaxed);
                    while (true)
                    {
                        Slot& slot = slots_[position & K_MASK];
                        const std::size_t sequence = slot.sequence.load(std::memory_order_acquire);
                        const std::intptr_t lag = static_cast<std::intptr_t>(sequence - position);
                        if (lag == 0)
                        {
                            // The weak CAS reloads position on failure, so the loop retries the newer head.
                            if (head_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                            {
                                slot.value = std::move(value);
                                slot.sequence.store(position + 1, std::memory_order_release);
                                return true;
                            }
                        }
                        else if (lag < 0)
                            return false;
                        else
                            position = head_.load(std::memory_order_relaxed);
                    }
                }

                /**
                 * @brief Pops the oldest published value; only the one consumer thread may call it.
                 * @param out Receives the value on success; untouched otherwise.
                 * @return False when the oldest slot is empty or its producer has not published it yet.
                 */
                [[nodiscard]] bool try_pop(Value& out)
                {
                    Slot& slot = slots_[tail_ & K_MASK];
                    if (slot.sequence.load(std::memory_order_acquire) != tail_ + 1)
                        return false;
                    out = std::move(slot.value);
                    slot.sequence.store(tail_ + Capacity, std::memory_order_release);
                    ++tail_;
                    return true;
                }

            private:
                /** @brief Masks a position into a slot index. */
                static constexpr std::size_t K_MASK = Capacity - 1;

                /** @brief Holds one value and the position whose push or pop it waits for. */
                struct Slot
                {
                    std::atomic<std::size_t> sequence{0}; /**< Position + 1 once published, + Capacity once popped. */
                    Value value{};                        /**< The value; valid only while published. */
                };

                /// @brief The slots, indexed by position modulo the capacity.
                std::unique_ptr<Slot[]> slots_;
                /// @brief The next position a producer claims.
                alignas(SushiRuntime::K_CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
                /// @brief The next position the consumer pops; written by the consumer alone.
                alignas(SushiRuntime::K_CACHE_LINE_SIZE) std::size_t tail_ = 0;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
