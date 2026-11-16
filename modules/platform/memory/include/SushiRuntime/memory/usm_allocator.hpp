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
 * @file usm_allocator.hpp
 * @brief Declares USMAllocator, which allocates and frees USM on a set of devices.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/device_contexts.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/memory/usm_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Allocates and frees USM from a source of a fixed set of devices; keeps no table.
         *
         * Any thread may call any method; the caller orders two calls on one address.
         * @see modules/platform/memory/README.md
         */
        class SUSHIRUNTIME_API USMAllocator final : public DeviceContexts
        {
            public:
                /**
                 * @brief Takes the source to allocate from; a device's index is the source's.
                 * @param source Not null; the allocator owns it for its whole life.
                 * @param collector The collector the allocator's zones and values record into; it must
                 *                  outlive the allocator.
                 */
                explicit USMAllocator(std::unique_ptr<USMSource> source,
                                      Profile::Collector& collector = Profile::process_collector());

                /** @brief Leaves live allocations alone; their owners free them. */
                ~USMAllocator() override = default;

                USMAllocator(const USMAllocator&) = delete;
                USMAllocator& operator=(const USMAllocator&) = delete;
                USMAllocator(USMAllocator&&) = delete;
                USMAllocator& operator=(USMAllocator&&) = delete;

                /**
                 * @brief Allocates USM of the requested kind on the requested device.
                 * @param request Alignment 0 asks for none.
                 * @return The allocation, or null when the source could not satisfy it.
                 * @pre request.device_index names a device of the allocator.
                 */
                [[nodiscard]] void* allocate(const USMRequest& request);

                /**
                 * @brief Frees an address on the source, against the device it was allocated on.
                 * @param address Null is ignored. A free that throws is logged as one ERROR and
                 *        the memory stays allocated.
                 * @param device The device allocate() was asked for; one the allocator does not
                 *        hold frees nothing and logs one ERROR.
                 * @see modules/platform/memory/README.md
                 */
                void release(void* address, std::size_t device) noexcept;

                /**
                 * @brief Copies bytes on a device and returns when they have arrived.
                 * @param device The device that owns the device side of the copy.
                 * @pre device names a device of the allocator; both ranges hold bytes.
                 */
                void copy(std::size_t device, void* destination, const void* source,
                          std::size_t bytes);

                /** @brief Counts the devices of the allocator, fixed at construction. */
                [[nodiscard]] std::size_t device_count() const noexcept override;

                /**
                 * @brief Tells whether two devices of the source share one memory context.
                 * @pre Both indices name devices of the allocator.
                 * @see modules/platform/memory/README.md
                 */
                [[nodiscard]] bool shares_context(std::size_t first, std::size_t second) const override;

                /**
                 * @brief Returns the kind of USM an address is on a device, as the source knows it.
                 * @return The kind, or nothing for memory the source does not know.
                 * @pre device names a device of the allocator.
                 */
                [[nodiscard]] std::optional<USMKind> kind_of(const void* address, std::size_t device) const;

            private:
                /**
                 * @brief Fails a device index the allocator does not hold.
                 * @param device_index The index a caller passed.
                 */
                void require_device(std::size_t device_index) const;

                /** @brief The memory of every device, fixed at construction; never null. */
                std::unique_ptr<USMSource> source_;

                /** @brief The collector the allocator's zones and values record into. */
                Profile::Collector& collector_;
        };

    } // namespace Internal::Memory
} // namespace SushiRuntime
