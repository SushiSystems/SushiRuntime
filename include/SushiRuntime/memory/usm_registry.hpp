/**************************************************************************/
/* usm_registry.hpp                                                       */
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

#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Remembers which device each USM pointer was allocated on.
         *
         * USM is bound to the context it was allocated from: a pointer must be freed
         * against that device's queue, and a kernel touching it must run there. SYCL
         * offers no way to ask a pointer where it came from that is reliable across
         * backends, so the runtime records it at allocation and looks it up later.
         *
         * Separated from RuntimeContext because it is the one part of it with an
         * invariant of its own — a pointer is known from allocation until it is
         * freed, exactly once — and that invariant is testable without standing up a
         * Runtime, which is not true of anything else RuntimeContext does.
         *
         * Every operation takes the same lock. That is not a bottleneck in practice:
         * allocation happens when the caller constructs a Buffer or State, which is
         * a setup-time act, and a measured run allocates nothing per step.
         */
        class SUSHIRUNTIME_API UsmRegistry
        {
            public:
                /**
                 * @brief Records the device a freshly allocated pointer belongs to.
                 *
                 * Recording a pointer that is already known replaces its owner rather
                 * than rejecting it, which matches an allocator handing back an
                 * address whose free was never reported.
                 *
                 * @param ptr          The allocation. Null is ignored.
                 * @param device_index Index of the owning device, as the caller
                 *                     numbers its queues; never interpreted here.
                 */
                void record(void* ptr, std::size_t device_index)
                {
                    if (!ptr)
                        return;
                    std::lock_guard<std::mutex> lock(mutex_);
                    owners_[ptr] = device_index;
                }

                /**
                 * @brief Looks up the device a pointer was allocated on.
                 *
                 * @param ptr The allocation to resolve. Null resolves to nothing.
                 * @return The recorded device index, or nothing when the pointer was
                 *         not allocated through this registry — which a caller must
                 *         treat as "unknown", not as device zero.
                 */
                std::optional<std::size_t> owner_of(void* ptr) const
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = owners_.find(ptr);
                    return it == owners_.end() ? std::nullopt
                                               : std::optional<std::size_t>(it->second);
                }

                /**
                 * @brief Resolves a pointer's device and forgets it, in one step.
                 *
                 * The free path needs both halves and must not be interruptible
                 * between them: looking up and then erasing under two separate locks
                 * would let a second free of the same pointer read an owner the first
                 * is already acting on.
                 *
                 * @param ptr The allocation being freed. Null resolves to nothing.
                 * @return The device index the pointer was recorded under, or nothing
                 *         if it was unknown — which tells a double free from a first.
                 */
                std::optional<std::size_t> take(void* ptr)
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = owners_.find(ptr);
                    if (it == owners_.end())
                        return std::nullopt;
                    const std::size_t owner = it->second;
                    owners_.erase(it);
                    return owner;
                }

                /**
                 * @brief Number of allocations currently recorded.
                 *
                 * @return Live entries: pointers recorded and not yet taken. A
                 *         non-zero count at teardown means an allocation outlived
                 *         its runtime.
                 */
                std::size_t size() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return owners_.size();
                }

            private:
                mutable std::mutex mutex_;
                std::unordered_map<void*, std::size_t> owners_;
        };

    } // namespace Memory
} // namespace SushiRuntime
