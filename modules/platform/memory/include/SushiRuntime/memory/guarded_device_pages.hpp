/**************************************************************************/
/* guarded_device_pages.hpp                                               */
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
 * @file guarded_device_pages.hpp
 * @brief Declares GuardedDevicePages, device memory on pages the host cannot touch while no runtime work runs.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/memory/device_access_gate.hpp>
#include <SushiRuntime/memory/page_protection.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Maps each allocation on pages of its own, inaccessible while the gate is closed and
         *        read-write while it is open.
         *
         * Any thread may call any method. A host access to an allocation while the gate is closed
         * faults.
         * @see modules/platform/memory/README.md
         */
        class SUSHIRUNTIME_API GuardedDevicePages final : public DeviceAccessGate
        {
            public:
                /** @brief Makes a closed gate with no allocation. */
                GuardedDevicePages();

                GuardedDevicePages(const GuardedDevicePages&) = delete;
                GuardedDevicePages& operator=(const GuardedDevicePages&) = delete;
                GuardedDevicePages(GuardedDevicePages&&) = delete;
                GuardedDevicePages& operator=(GuardedDevicePages&&) = delete;

                /** @brief Frees nothing; an allocation still live belongs to an owner that outlived the pages. */
                ~GuardedDevicePages() override = default;

                /**
                 * @brief Maps whole pages for a request, accessible only while the gate is open.
                 * @param alignment 0 for none; otherwise a power of two no larger than a page.
                 * @return The first byte, on a page boundary, or null for a refused alignment, a size
                 *         no mapping can hold or a mapping the system refuses.
                 */
                [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);

                /**
                 * @brief Unmaps an allocation.
                 * @pre address is a live allocation of these pages.
                 */
                void release(void* address) noexcept;

                /** @brief Reports whether an address lies on the pages of a live allocation. */
                [[nodiscard]] bool holds(const void* address) const;

                /** @brief Reports whether the gate is open. */
                [[nodiscard]] bool is_open() const;

                void open() noexcept override;
                void close() noexcept override;

            private:
                /** @brief Sets the access of every live allocation; a refusal stops the process. */
                void protect_all(PageAccess access) noexcept;

                /** @brief The bytes of one page. */
                std::size_t page_bytes_;

                /** @brief Guards live_ and open_count_. */
                mutable std::mutex mutex_;

                /** @brief The mapped bytes of each live allocation, by its first byte. */
                std::map<std::uintptr_t, std::size_t> live_;

                /** @brief The opens not yet closed; the pages are accessible while it is above 0. */
                std::size_t open_count_ = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
