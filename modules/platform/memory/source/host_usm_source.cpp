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
 * @brief Defines HostUSMSource, which allocates every kind of USM from the host heap, and its factory.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/host_usm_source.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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

            /**
             * @brief Allocates every kind of USM from the host heap, for a fixed number of devices.
             *
             * Every device shares one context. It records the range and kind of each live allocation, so
             * kind_of() answers as a device context would.
             */
            class HostUSMSource final : public USMSource
            {
                public:
                    /** @brief Makes a source of a number of host devices; at least 1. */
                    explicit HostUSMSource(std::size_t device_count);

                    [[nodiscard]] std::size_t device_count() const noexcept override;
                    [[nodiscard]] void* allocate(const USMRequest& request) override;
                    void free(void* address, std::size_t device_index) override;
                    [[nodiscard]] bool shares_context(std::size_t first,
                                                      std::size_t second) const override;
                    void copy(void* destination, const void* source, std::size_t bytes,
                              std::size_t device_index) override;
                    [[nodiscard]] std::string device_name(std::size_t index) const override;
                    [[nodiscard]] std::optional<USMKind> kind_of(const void* address,
                                                                 std::size_t device_index) const override;

                private:
                    /** @brief Holds the end and kind of one live allocation. */
                    struct Live
                    {
                        std::uintptr_t end = 0;      /**< One past the allocation's last byte. */
                        USMKind kind = USMKind::Host; /**< Kind the allocation was asked for. */
                    };

                    /** @brief The number of devices the source stands for. */
                    std::size_t device_count_;

                    /** @brief Guards live_. */
                    mutable std::mutex mutex_;

                    /** @brief Live allocations by their first byte. */
                    std::map<std::uintptr_t, Live> live_;
            };
        } // namespace

        HostUSMSource::HostUSMSource(std::size_t device_count) : device_count_(device_count)
        {
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
            // A block over PTRDIFF_MAX bytes, larger than any object can be, is refused before the heap is asked.
            const auto largest = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
            if (overhead > largest || request.bytes > largest - overhead)
                return nullptr;

            void* const raw = std::malloc(request.bytes + overhead);
            if (raw == nullptr)
                return nullptr;

            // The heap block's own address sits in the word below the aligned one, for free().
            const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
            const std::uintptr_t aligned = (start + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
            void** const slot = reinterpret_cast<void**>(aligned);
            slot[-1] = raw;
            const std::lock_guard<std::mutex> lock(mutex_);
            live_[aligned] = Live{aligned + request.bytes, request.kind};
            return slot;
        }

        void HostUSMSource::free(void* address, std::size_t device_index)
        {
            require_device(device_index, device_count_);
            if (address == nullptr)
                return;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                live_.erase(reinterpret_cast<std::uintptr_t>(address));
            }
            std::free(static_cast<void**>(address)[-1]);
        }

        bool HostUSMSource::shares_context(std::size_t first, std::size_t second) const
        {
            require_device(first, device_count_);
            require_device(second, device_count_);
            return true;
        }

        void HostUSMSource::copy(void* destination, const void* source, std::size_t bytes,
                                 std::size_t device_index)
        {
            require_device(device_index, device_count_);
            std::memcpy(destination, source, bytes);
        }

        std::string HostUSMSource::device_name(std::size_t index) const
        {
            require_device(index, device_count_);
            return "host device " + std::to_string(index);
        }

        std::optional<USMKind> HostUSMSource::kind_of(const void* address, std::size_t device_index) const
        {
            require_device(device_index, device_count_);
            const auto at = reinterpret_cast<std::uintptr_t>(address);
            const std::lock_guard<std::mutex> lock(mutex_);
            auto found = live_.upper_bound(at);
            if (found == live_.begin())
                return std::nullopt;
            found = std::prev(found);
            if (at >= found->second.end && at != found->first)
                return std::nullopt;
            return found->second.kind;
        }

        std::unique_ptr<USMSource> make_host_usm_source(std::size_t device_count)
        {
            return std::make_unique<HostUSMSource>(device_count);
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
