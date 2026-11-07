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
 * @file usm_allocator.cpp
 * @brief Defines USMAllocator: the SYCL call per request, the records and the release path.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/usm_allocator.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/memory/memory_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Returns the name of a USM kind as the refusal line prints it. */
            const char* kind_name(USMKind kind) noexcept
            {
                switch (kind)
                {
                    case USMKind::Host:
                        return "host";
                    case USMKind::Shared:
                        return "shared";
                    case USMKind::Device:
                        return "device";
                }
                return "unknown";
            }

            /** @brief Logs one WARN for a request the source answered with null. */
            void log_refused_request(const USMRequest& request, const USMSource& source)
            {
                SR_LOG_WARN(K_MEMORY_LOG_CATEGORY,
                            "The source refused a {} USM allocation of {} bytes, alignment {}, on "
                            "device {} ({}); returning null to the caller.",
                            kind_name(request.kind), request.bytes, request.alignment,
                            request.device_index, source.device_name(request.device_index));
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

        USMAllocator::USMAllocator(std::unique_ptr<USMSource> source) : source_(std::move(source))
        {
            SR_VERIFY(source_ != nullptr, "USMAllocator: pass a USM source, not null.");
        }

        void* USMAllocator::allocate(const USMRequest& request)
        {
            SR_ZONE_FINE("memory.allocate");
            SR_VALUE("memory.allocate_bytes", "bytes", static_cast<double>(request.bytes));

            require_device(request.device_index);
            void* const address = source_->allocate(request);
            if (!address)
            {
                log_refused_request(request, *source_);
                return nullptr;
            }

            registry_.record(address, request.device_index, request.bytes, request.kind);
            // A mark left by an adopted mapping that once sat at this address would leak it.
            adopted_.take(address);
            return address;
        }

        void USMAllocator::release(void* address) noexcept
        {
            if (!address)
                return;

            SR_ZONE_FINE("memory.release");

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

            if (device.has_value() && *device < source_->device_count())
            {
                source_->free(address, *device);
                return;
            }

            if (source_->device_count() == 0)
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
            require_device(device_index);

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
            if (device.has_value() && *device < source_->device_count())
                return device;
            return std::nullopt;
        }

        std::optional<USMRegistry::Record> USMAllocator::record_of(const void* address) const
        {
            const std::optional<USMRegistry::Record> record = registry_.record_of(address);
            if (record.has_value() && record->device_index < source_->device_count())
                return record;
            return std::nullopt;
        }

        void USMAllocator::copy(const void* base, std::size_t device, void* destination,
                                const void* source, std::size_t bytes)
        {
            const std::optional<std::size_t> owner = owning_device(base);
            source_->copy(destination, source, bytes, owner.value_or(device));
        }

        std::optional<std::size_t> USMAllocator::owning_device(const void* address) const
        {
            if (!address)
                return std::nullopt;

            if (const std::optional<std::size_t> recorded = device_of(address))
                return recorded;

            return source_->resolve_owner(address);
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
            return source_->device_count();
        }

        bool USMAllocator::shares_context(std::size_t first, std::size_t second) const
        {
            require_device(first);
            require_device(second);
            return source_->shares_context(first, second);
        }

        void USMAllocator::require_device(std::size_t device_index) const
        {
            SR_VERIFY(device_index < source_->device_count(),
                      ("USM device index " + std::to_string(device_index) +
                       " is out of range: the allocator holds " +
                       std::to_string(source_->device_count()) + " device(s).")
                          .c_str());
        }

    } // namespace Memory
} // namespace SushiRuntime
