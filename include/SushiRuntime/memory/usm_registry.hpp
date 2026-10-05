/****************************************************************/
/* usm_registry.hpp                                             */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
                 * @brief Names the allocation an interior pointer falls inside.
                 *
                 * What containing_allocation() answers with: the address the
                 * allocation starts at, which owner_of() resolves, and the device
                 * that owns it.
                 */
                struct Containment
                {
                    /** @brief First address of the allocation. */
                    void* base = nullptr;

                    /** @brief Index of the device the allocation belongs to. */
                    std::size_t device_index = 0;
                };

                /**
                 * @brief Records the device and extent a freshly allocated pointer has.
                 *
                 * Recording a pointer that is already known replaces its entry rather
                 * than rejecting it, which matches an allocator handing back an
                 * address whose free was never reported.
                 *
                 * @param ptr          The allocation. Null is ignored.
                 * @param device_index Index of the owning device, as the caller
                 *                     numbers its queues; never interpreted here.
                 * @param bytes        Length of the allocation, or nothing when the
                 *                     caller cannot know it — an imported address
                 *                     whose producer named no size. An entry without
                 *                     a length resolves at its base and contains
                 *                     nothing, so a missing size never reads as zero.
                 */
                void record(void* ptr, std::size_t device_index, std::optional<std::size_t> bytes)
                {
                    if (!ptr)
                        return;
                    std::lock_guard<std::mutex> lock(mutex_);
                    owners_[ptr] = Entry{device_index, bytes};
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
                                               : std::optional<std::size_t>(it->second.device_index);
                }

                /**
                 * @brief Finds the recorded allocation a pointer falls inside.
                 *
                 * Scans every entry, because the table is keyed by base address and
                 * an interior pointer hashes nowhere near its own allocation. That
                 * cost is linear in live allocations and is why this is a separate
                 * query rather than a widening of owner_of(): call it from
                 * registration paths, never from dispatch, which resolves exact base
                 * addresses through owner_of() in constant time.
                 *
                 * @param ptr Any address, interior or base. Null resolves to nothing.
                 * @return The allocation covering @p ptr, or nothing when no recorded
                 *         allocation covers it — including a pointer inside an entry
                 *         recorded without a length, which only its base resolves.
                 */
                std::optional<Containment> containing_allocation(void* ptr) const
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (const auto& entry : owners_)
                    {
                        if (entry.first == ptr)
                            return Containment{entry.first, entry.second.device_index};
                        if (!entry.second.bytes.has_value())
                            continue;
                        auto* const base = static_cast<const unsigned char*>(entry.first);
                        auto* const address = static_cast<const unsigned char*>(ptr);
                        if (address > base && address < base + *entry.second.bytes)
                            return Containment{entry.first, entry.second.device_index};
                    }
                    return std::nullopt;
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
                    const std::size_t owner = it->second.device_index;
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
                /** @brief What the table holds for one recorded allocation. */
                struct Entry
                {
                    /** @brief Index of the device the allocation belongs to. */
                    std::size_t device_index = 0;

                    /** @brief Length in bytes, or nothing when the caller had none. */
                    std::optional<std::size_t> bytes;
                };

                mutable std::mutex mutex_;
                std::unordered_map<void*, Entry> owners_;
        };

    } // namespace Memory
} // namespace SushiRuntime
