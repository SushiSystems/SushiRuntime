/**************************************************************************/
/* guarded_device_pages.cpp                                               */
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
 * @file guarded_device_pages.cpp
 * @brief Defines GuardedDevicePages, which maps device allocations on pages the gate opens and closes.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#include <SushiRuntime/memory/guarded_device_pages.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>

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
        } // namespace

        GuardedDevicePages::GuardedDevicePages() : page_bytes_(page_bytes())
        {
        }

        void* GuardedDevicePages::allocate(std::size_t bytes, std::size_t alignment)
        {
            if (alignment != 0 && (!is_power_of_two(alignment) || alignment > page_bytes_))
                return nullptr;
            // A zero-byte request still takes a page, so its address is its own.
            const std::size_t wanted = bytes == 0 ? 1 : bytes;
            const std::size_t pages = wanted / page_bytes_ + (wanted % page_bytes_ != 0 ? 1 : 0);
            if (pages > SIZE_MAX / page_bytes_)
                return nullptr;
            const std::size_t mapped = pages * page_bytes_;

            const std::lock_guard<std::mutex> lock(mutex_);
            void* const first = map_pages(mapped, open_count_ > 0 ? PageAccess::ReadWrite : PageAccess::None);
            if (first == nullptr)
                return nullptr;
            live_[reinterpret_cast<std::uintptr_t>(first)] = mapped;
            return first;
        }

        void GuardedDevicePages::release(void* address) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto found = live_.find(reinterpret_cast<std::uintptr_t>(address));
            SR_VERIFY(found != live_.end(),
                      "GuardedDevicePages::release: the address is no live allocation of these pages.");
            unmap_pages(address, found->second);
            live_.erase(found);
        }

        bool GuardedDevicePages::holds(const void* address) const
        {
            const auto at = reinterpret_cast<std::uintptr_t>(address);
            const std::lock_guard<std::mutex> lock(mutex_);
            auto found = live_.upper_bound(at);
            if (found == live_.begin())
                return false;
            found = std::prev(found);
            return at - found->first < found->second;
        }

        bool GuardedDevicePages::is_open() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return open_count_ > 0;
        }

        void GuardedDevicePages::open() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++open_count_;
            if (open_count_ == 1)
                protect_all(PageAccess::ReadWrite);
        }

        void GuardedDevicePages::close() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            SR_VERIFY(open_count_ > 0, "GuardedDevicePages::close: the gate is not open; each close() follows an open().");
            --open_count_;
            if (open_count_ == 0)
                protect_all(PageAccess::None);
        }

        void GuardedDevicePages::protect_all(PageAccess access) noexcept
        {
            for (const auto& [first, mapped] : live_)
            {
                const bool protected_now = protect_pages(reinterpret_cast<void*>(first), mapped, access);
                SR_VERIFY(protected_now,
                          "GuardedDevicePages: the system refused to change the access of guarded pages.");
            }
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
