/**************************************************************************/
/* stated_storage.hpp                                                     */
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
 * @file stated_storage.hpp
 * @brief Declares StatedStorage, the storage of memory pinned to one stated device.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include <SushiRuntime/api/data/buffer_storage.hpp>
#include <SushiRuntime/api/data/residency.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Keeps one allocation on one device, owned or borrowed, and copies through that
         *        device's queue.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        class SUSHIRUNTIME_API StatedStorage final : public BufferStorage
        {
            public:
                /**
                 * @brief Takes the allocation and states where it lives.
                 * @param block The memory; it releases what it owns.
                 * @param owner_life Expires when the owner of borrowed memory releases it; empty
                 *        for memory the storage owns.
                 * @pre residency is not Residency::Planned.
                 */
                StatedStorage(MemoryBlock block, DeviceIndex device, Residency residency,
                              LivenessObserver owner_life = LivenessObserver()) noexcept
                    : block_(std::move(block)),
                      device_(device),
                      residency_(residency),
                      owner_life_(std::move(owner_life))
                {

                }

                [[nodiscard]] void* address() const noexcept override
                {
                    return block_.allocation.address();
                }

                [[nodiscard]] std::optional<DeviceIndex> device_index() const noexcept override
                {
                    return device_;
                }

                [[nodiscard]] Residency residency() const noexcept override { return residency_; }

                [[nodiscard]] bool host_addressable() const noexcept override
                {
                    return is_host_addressable(residency_);
                }

                [[nodiscard]] const char* host_refusal() const noexcept override
                {
                    return "a device-resident buffer is not host-addressable; ";
                }

                [[nodiscard]] LivenessObserver owner_life() const override { return owner_life_; }

                /** @brief Copies on the queue of the stated device; fails with its error. */
                [[nodiscard]] Result<void> read(const ContextHandle& context, GlobalHandle handle,
                                                std::size_t begin, void* destination,
                                                std::size_t bytes) const noexcept override;

                /** @brief Copies on the queue of the stated device; fails with its error. */
                [[nodiscard]] Result<void> write(const ContextHandle& context, GlobalHandle handle,
                                                 std::size_t begin, const void* source,
                                                 std::size_t bytes) const noexcept override;

                /** @brief Retires the handle, or drops one borrow of a Borrowed handle. */
                void release(const ContextHandle& context, GlobalHandle handle) noexcept override;

            private:
                /** @brief The allocation, held in place; it releases the memory it owns. */
                MemoryBlock block_;

                /** @brief The device the allocation is pinned to. */
                DeviceIndex device_;

                /** @brief Where the allocation lives. */
                Residency residency_;

                /** @brief Observer of the owner of borrowed memory; empty for an owned block. */
                LivenessObserver owner_life_;
        };
    } // namespace Detail
} // namespace SushiRuntime
