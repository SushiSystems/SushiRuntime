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
 * @brief Defines USMAllocator: the SYCL call per request and the release path.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/usm_allocator.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
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
                                 "allocated.", address, reason);
                }
                catch (...)
                {
                    std::fputs("SushiRuntime: a USM release threw and the failure could not be "
                               "logged.\n", stderr);
                }
            }
        } // namespace

        USMAllocator::USMAllocator(std::unique_ptr<USMSource> source, Profile::Collector& collector)
            : source_(std::move(source)), collector_(collector)
        {
            SR_VERIFY(source_ != nullptr, "USMAllocator: pass a USM source, not null.");
        }

        void* USMAllocator::allocate(const USMRequest& request)
        {
            SR_ZONE_FINE(collector_, "memory.allocate");
            SR_VALUE(collector_, "memory.allocate_bytes", "bytes", static_cast<double>(request.bytes));

            require_device(request.device_index);
            void* const address = source_->allocate(request);
            if (!address)
            {
                log_refused_request(request, *source_);
                return nullptr;
            }

            return address;
        }

        void USMAllocator::release(void* address, std::size_t device) noexcept
        {
            if (!address)
                return;

            SR_ZONE_FINE(collector_, "memory.release");

            try
            {
                if (device < source_->device_count())
                {
                    source_->free(address, device);
                    return;
                }
                SR_LOG_ERROR(K_MEMORY_LOG_CATEGORY, "release of USM address {} names device {}, which the "
                             "allocator does not hold: nothing was freed.", address, device);
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

        void USMAllocator::copy(std::size_t device, void* destination, const void* source,
                                std::size_t bytes)
        {
            require_device(device);
            source_->copy(destination, source, bytes, device);
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
