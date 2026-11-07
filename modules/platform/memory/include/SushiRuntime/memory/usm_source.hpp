/**************************************************************************/
/* usm_source.hpp                                                         */
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
 * @file usm_source.hpp
 * @brief Declares USMSource, the port USMAllocator takes its memory from.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Makes and frees the bytes of USM allocations on a fixed set of devices.
         *
         * USMAllocator keeps the records; a source only moves memory. make_sycl_usm_source() makes
         * the device adapter and make_host_usm_source() the in-memory one; both pass
         * USMSourceContract.
         * @see modules/platform/memory/README.md
         */
        class USMSource
        {
            public:
                /** @brief Frees nothing; the allocator releases what it recorded first. */
                virtual ~USMSource() = default;

                /** @brief Returns the number of devices; a device's index is below it. */
                [[nodiscard]] virtual std::size_t device_count() const noexcept = 0;

                /**
                 * @brief Allocates the bytes of a request.
                 * @return The allocation, aligned to request.alignment when it is not 0, or null
                 *         when the source has no memory for it.
                 * @pre request.device_index is below device_count().
                 */
                [[nodiscard]] virtual void* allocate(const USMRequest& request) = 0;

                /**
                 * @brief Frees an address allocate() returned for a device.
                 * @pre address came from allocate() with this device index and is not freed.
                 */
                virtual void free(void* address, std::size_t device_index) = 0;

                /**
                 * @brief Returns the device that owns a shared or device allocation of the source's
                 *        devices, an address inside one included.
                 * @return The device, or nothing for host memory: a Host allocation, a stack or heap
                 *         address, and an address no device of the source owns.
                 */
                [[nodiscard]] virtual std::optional<std::size_t>
                resolve_owner(const void* address) const = 0;

                /**
                 * @brief Reports whether two devices share one memory context.
                 * @pre Both indices are below device_count().
                 */
                [[nodiscard]] virtual bool shares_context(std::size_t first,
                                                          std::size_t second) const = 0;

                /**
                 * @brief Copies bytes between two addresses, on the device that does the copy, and
                 *        returns when they have arrived.
                 * @param device_index The device whose memory engine copies.
                 * @pre device_index is below device_count(); both ranges hold bytes and do not
                 *      overlap.
                 */
                virtual void copy(void* destination, const void* source, std::size_t bytes,
                                  std::size_t device_index) = 0;

                /**
                 * @brief Returns the name of a device, as a refusal line prints it.
                 * @pre index is below device_count().
                 */
                [[nodiscard]] virtual std::string device_name(std::size_t index) const = 0;

            protected:
                USMSource() = default;
                USMSource(const USMSource&) = default;
                USMSource& operator=(const USMSource&) = default;
                USMSource(USMSource&&) = default;
                USMSource& operator=(USMSource&&) = default;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
