/**************************************************************************/
/* usm_allocator.cpp                                                      */
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
 * @file usm_allocator.cpp
 * @brief Defines USMAllocator: the SYCL call per request, the records and the release path.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/usm_allocator.hpp>

#include <cstdio>
#include <exception>
#include <string>
#include <thread>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/memory/memory_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Makes the unaligned SYCL allocation of a kind; null when SYCL has none. */
            void* allocate_unaligned(USMKind kind, std::size_t bytes, const sycl::queue& queue)
            {
                switch (kind)
                {
                    case USMKind::Host:
                        return sycl::malloc_host(bytes, queue);
                    case USMKind::Shared:
                        return sycl::malloc_shared(bytes, queue);
                    case USMKind::Device:
                        return sycl::malloc_device(bytes, queue);
                }
                return nullptr;
            }

            /** @brief Makes the aligned SYCL allocation of a kind; null when SYCL has none. */
            void* allocate_aligned(USMKind kind, std::size_t alignment, std::size_t bytes,
                                   const sycl::queue& queue)
            {
                switch (kind)
                {
                    case USMKind::Host:
                        return sycl::aligned_alloc_host(alignment, bytes, queue);
                    case USMKind::Shared:
                        return sycl::aligned_alloc_shared(alignment, bytes, queue);
                    case USMKind::Device:
                        return sycl::aligned_alloc_device(alignment, bytes, queue);
                }
                return nullptr;
            }

            /** @brief Logs one ERROR for a release that threw; falls back to stderr. */
            void report_failed_release(void* address, const char* reason) noexcept
            {
                try
                {
                    SR_LOG_ERROR(K_MEMORY_LOG_CATEGORY, "release of USM address {} threw '{}': the address may still be "
                                 "allocated and is no longer tracked.", address, reason);
                }
                catch (...)
                {
                    std::fputs("SushiRuntime: a USM release threw and its record could not be "
                               "logged.\n", stderr);
                }
            }
        } // namespace

        USMAllocator::USMAllocator(std::vector<sycl::queue> device_queues)
            : queues_(std::move(device_queues))
        {

        }

        void* USMAllocator::allocate(const USMRequest& request)
        {
            const sycl::queue& queue = queue_at(request.device_index);
            void* const address = request.alignment == 0
                ? allocate_unaligned(request.kind, request.bytes, queue)
                : allocate_aligned(request.kind, request.alignment, request.bytes, queue);
            if (!address)
                return nullptr;

            registry_.record(address, request.device_index, request.bytes, request.kind);
            // A mark left by an adopted mapping that once sat at this address would leak it.
            adopted_.take(address);
            return address;
        }

        void USMAllocator::release(void* address) noexcept
        {
            if (!address)
                return;

            // Counted before the load: zero read after a store means no call holds the old one.
            observer_calls_in_flight_.fetch_add(1, std::memory_order_seq_cst);
            if (USMReleaseObserver* const observer =
                    release_observer_.load(std::memory_order_seq_cst))
                observer->on_release(address);
            observer_calls_in_flight_.fetch_sub(1, std::memory_order_seq_cst);

            try
            {
                release_recorded(address);
            }
            catch (const std::exception& error)
            {
                report_failed_release(address, error.what());
            }
            catch (...)
            {
                report_failed_release(address, "an exception that is not a std::exception");
            }
        }

        void USMAllocator::release_recorded(void* address)
        {
            const std::optional<std::size_t> device = registry_.take(address);
            if (adopted_.take(address).has_value())
                return;

            if (device.has_value() && *device < queues_.size())
            {
                sycl::free(address, queues_[*device]);
                return;
            }

            if (queues_.empty())
            {
                SR_LOG_WARN(K_MEMORY_LOG_CATEGORY, "release of USM address {} with no record: the allocator holds no "
                            "device, so nothing was freed.", address);
                return;
            }

            SR_LOG_ERROR(K_MEMORY_LOG_CATEGORY, "release of USM address {} with no record (second release or foreign "
                         "pointer): nothing was freed.", address);
        }

        void USMAllocator::adopt(void* address, std::size_t device_index,
                                 std::optional<std::size_t> bytes, std::optional<USMKind> kind)
        {
            static_cast<void>(queue_at(device_index));

            // Marked before it is recorded: a recorded adopted address is never seen unmarked.
            adopted_.record(address, device_index, bytes, kind);
            registry_.record(address, device_index, bytes, kind);
        }

        void USMAllocator::forget(void* address) noexcept
        {
            registry_.take(address);
            adopted_.take(address);
        }

        std::optional<std::size_t> USMAllocator::device_of(const void* address) const
        {
            const std::optional<std::size_t> device = registry_.owner_of(address);
            if (device.has_value() && *device < queues_.size())
                return device;
            return std::nullopt;
        }

        std::optional<USMRegistry::Record> USMAllocator::record_of(const void* address) const
        {
            const std::optional<USMRegistry::Record> record = registry_.record_of(address);
            if (record.has_value() && record->device_index < queues_.size())
                return record;
            return std::nullopt;
        }

        std::optional<std::size_t> USMAllocator::owning_device(const void* address) const
        {
            if (!address)
                return std::nullopt;

            if (const std::optional<std::size_t> recorded = device_of(address))
                return recorded;

            std::optional<std::size_t> best;
            for (std::size_t index = 0; index < queues_.size(); ++index)
            {
                const sycl::queue& queue = queues_[index];
                try
                {
                    const sycl::context context = queue.get_context();
                    const sycl::usm::alloc kind = sycl::get_pointer_type(address, context);
                    if (kind == sycl::usm::alloc::unknown || kind == sycl::usm::alloc::host)
                        continue;
                    if (sycl::get_pointer_device(address, context) != queue.get_device())
                        continue;
                    // A shared match wins at once; a device match waits for a later shared one.
                    if (kind == sycl::usm::alloc::shared)
                        return index;
                    best = index;
                }
                catch (const sycl::exception&)
                {
                    // This context cannot resolve the address, which answers "not this device".
                    continue;
                }
            }
            return best;
        }

        std::optional<const void*> USMAllocator::containing_base(const void* address) const
        {
            const std::optional<USMRegistry::Containment> found =
                registry_.containing_allocation(address);
            return found.has_value() ? std::optional<const void*>(found->base) : std::nullopt;
        }

        std::size_t USMAllocator::live_count() const
        {
            return registry_.size();
        }

        void USMAllocator::set_release_observer(USMReleaseObserver* observer) noexcept
        {
            release_observer_.store(observer, std::memory_order_seq_cst);
            // Stored before the count is read: a release() counted later loads the new observer.
            while (observer_calls_in_flight_.load(std::memory_order_seq_cst) != 0)
                std::this_thread::yield();
        }

        std::size_t USMAllocator::device_count() const noexcept
        {
            return queues_.size();
        }

        bool USMAllocator::shares_context(std::size_t first, std::size_t second) const
        {
            return queue_at(first).get_context() == queue_at(second).get_context();
        }

        const sycl::queue& USMAllocator::queue_at(std::size_t device_index) const
        {
            SR_VERIFY(device_index < queues_.size(),
                      ("USM device index " + std::to_string(device_index) +
                       " is out of range: the allocator holds " +
                       std::to_string(queues_.size()) + " device(s).")
                          .c_str());
            return queues_[device_index];
        }

    } // namespace Memory
} // namespace SushiRuntime
