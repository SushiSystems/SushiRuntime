/**************************************************************************/
/* usm_allocator.hpp                                                      */
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
 * @file usm_allocator.hpp
 * @brief Declares UsmAllocator, which allocates, records and releases USM on a set of devices.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/usm_registry.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Allocates USM on one of a fixed set of device queues and remembers its device.
         *
         * Every method may be called from any thread at any time. Two calls that name the
         * same address are ordered by the caller.
         * @see modules/platform/memory/README.md
         */
        class SUSHIRUNTIME_API UsmAllocator final
        {
            public:
                /**
                 * @brief Takes the queues to allocate on; a device's index is its position here.
                 * @param device_queues Copies that share state with the caller's queues. The list
                 *        is fixed for the allocator's lifetime.
                 */
                explicit UsmAllocator(std::vector<sycl::queue> device_queues);

                /** @brief Leaves live allocations alone; live_count() says how many remain. */
                ~UsmAllocator() = default;

                UsmAllocator(const UsmAllocator&) = delete;
                UsmAllocator& operator=(const UsmAllocator&) = delete;
                UsmAllocator(UsmAllocator&&) = delete;
                UsmAllocator& operator=(UsmAllocator&&) = delete;

                /**
                 * @brief Allocates USM of the requested kind on the requested device, recorded.
                 * @param request Alignment 0 asks for none.
                 * @return The allocation, or null when SYCL could not satisfy it.
                 * @throws Core::SushiException With Errc::invalid_access, the index and the device
                 *         count, when request.device_index names no device.
                 */
                [[nodiscard]] void* allocate(const UsmRequest& request);

                /**
                 * @brief Forgets an address and frees it against the queue of its device.
                 * @param address Null is ignored. An adopted address is forgotten and not freed.
                 *        An address with no record is freed against device 0, with one WARN.
                 * @see modules/platform/memory/README.md
                 */
                void release(void* address);

                /**
                 * @brief Records memory allocated elsewhere as belonging to a device.
                 * @param address Stays the caller's to free; release() will not free it.
                 * @param bytes Length of the mapping, or nothing when it is unknown.
                 * @throws Core::SushiException As allocate(), when device_index names no device.
                 */
                void adopt(void* address, std::size_t device_index,
                           std::optional<std::size_t> bytes);

                /** @brief Drops the record of an address without freeing it; null is ignored. */
                void forget(void* address) noexcept;

                /**
                 * @brief Returns the device an address was recorded on, in constant time.
                 * @param address The first address of an allocation; an interior one is unknown.
                 * @return Nothing for null and for an address with no record.
                 */
                [[nodiscard]] std::optional<std::size_t> device_of(void* address) const;

                /**
                 * @brief Returns the device of an address, asking SYCL when no record exists.
                 * @return Nothing for null, for host USM and for memory no queue's context knows.
                 * @see modules/platform/memory/README.md
                 */
                [[nodiscard]] std::optional<std::size_t> owning_device(void* address) const;

                /**
                 * @brief Finds the first address of the recorded allocation an address falls in.
                 * @param address Any address, interior or first. The search is linear in
                 *        live_count().
                 * @return Nothing for null and when no recorded allocation covers the address.
                 */
                [[nodiscard]] std::optional<void*> containing_base(void* address) const;

                /** @brief Counts the addresses recorded and not yet released or forgotten. */
                [[nodiscard]] std::size_t live_count() const;

                /**
                 * @brief Installs the observer release() tells, replacing the one before it.
                 * @param observer Null removes it. It must stay alive until it is removed and
                 *        every release() that began before the removal has returned.
                 */
                void set_release_observer(UsmReleaseObserver* observer) noexcept;

            private:
                /**
                 * @brief Returns the queue of a device.
                 * @throws Core::SushiException With Errc::invalid_access when the index names none.
                 */
                [[nodiscard]] const sycl::queue& queue_at(std::size_t device_index) const;

                /** @brief One queue per device, fixed at construction. */
                std::vector<sycl::queue> queues_;

                /** @brief Every live address, allocated here or adopted, and its device. */
                UsmRegistry registry_;

                /** @brief The adopted subset of registry_: addresses release() must not free. */
                UsmRegistry adopted_;

                /** @brief The observer release() tells, or null. */
                std::atomic<UsmReleaseObserver*> release_observer_{nullptr};
        };

    } // namespace Memory
} // namespace SushiRuntime
