/**************************************************************************/
/* host_usm_source.cpp                                                    */
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
 * @file host_usm_source.cpp
 * @brief Defines HostUSMSource, which allocates every kind of USM from the host heap.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/host_usm_source.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

#include <SushiRuntime/core/assert.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Reports whether a value is a power of two. */
            [[nodiscard]] bool is_power_of_two(std::size_t value) noexcept
            {
                return value != 0 && (value & (value - 1)) == 0;
            }

            /** @brief Fails a device index the source does not hold. */
            void require_device(std::size_t index, std::size_t count)
            {
                SR_VERIFY(index < count,
                          ("HostUSMSource: device index " + std::to_string(index) +
                           " is out of range: the source holds " + std::to_string(count) +
                           " device(s).")
                              .c_str());
            }
        } // namespace

        HostUSMSource::HostUSMSource(std::size_t device_count) : device_count_(device_count)
        {
            SR_VERIFY(device_count > 0, "HostUSMSource: pass a device count of at least 1.");
        }

        std::size_t HostUSMSource::device_count() const noexcept
        {
            return device_count_;
        }

        void* HostUSMSource::allocate(const USMRequest& request)
        {
            require_device(request.device_index, device_count_);
            const std::size_t alignment =
                request.alignment == 0 ? alignof(std::max_align_t) : request.alignment;
            if (!is_power_of_two(alignment))
                return nullptr;
            const std::size_t overhead = alignment + sizeof(void*);
            if (request.bytes > std::numeric_limits<std::size_t>::max() - overhead)
                return nullptr;

            void* const raw = std::malloc(request.bytes + overhead);
            if (raw == nullptr)
                return nullptr;

            // The heap block's own address sits in the word below the aligned one, for free().
            const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
            const std::uintptr_t aligned = (start + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
            void** const slot = reinterpret_cast<void**>(aligned);
            slot[-1] = raw;
            return slot;
        }

        void HostUSMSource::free(void* address, std::size_t device_index)
        {
            require_device(device_index, device_count_);
            if (address == nullptr)
                return;
            std::free(static_cast<void**>(address)[-1]);
        }

        std::optional<std::size_t> HostUSMSource::resolve_owner(const void* /*address*/) const
        {
            return std::nullopt;
        }

        bool HostUSMSource::shares_context(std::size_t first, std::size_t second) const
        {
            require_device(first, device_count_);
            require_device(second, device_count_);
            return true;
        }

        std::string HostUSMSource::device_name(std::size_t index) const
        {
            require_device(index, device_count_);
            return "host device " + std::to_string(index);
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
