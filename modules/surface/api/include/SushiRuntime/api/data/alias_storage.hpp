/**************************************************************************/
/* alias_storage.hpp                                                      */
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
 * @file alias_storage.hpp
 * @brief Declares AliasStorage, the storage of a view over the memory of another live buffer.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include <SushiRuntime/api/data/buffer_storage.hpp>
#include <SushiRuntime/api/data/residency.hpp>
#include <SushiRuntime/api/data/stated_storage.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Views the memory of another live buffer under that buffer's handle, and
         *        releases nothing.
         *
         * The buffer it views keeps the allocation and the handle; the view only reaches them.
         * Every query and copy is the stated storage's over the same bytes.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        class AliasStorage final : public BufferStorage
        {
            public:
                /**
                 * @brief Views @p bytes at @p address on the source buffer's device.
                 * @param source_life Expires when the source buffer releases the memory.
                 * @pre residency is not Residency::Planned, and address names bytes bytes.
                 */
                AliasStorage(void* address, std::size_t bytes, DeviceIndex device,
                             Residency residency, LivenessObserver source_life) noexcept
                    : viewed_(MemoryBlock{Allocation(address, bytes, nullptr)}, device, residency,
                              std::move(source_life))
                {
                }

                [[nodiscard]] void* address() const noexcept override
                {
                    return viewed_.address();
                }

                [[nodiscard]] std::optional<DeviceIndex> device_index() const noexcept override
                {
                    return viewed_.device_index();
                }

                [[nodiscard]] Residency residency() const noexcept override
                {
                    return viewed_.residency();
                }

                [[nodiscard]] bool host_addressable() const noexcept override
                {
                    return viewed_.host_addressable();
                }

                [[nodiscard]] const char* host_refusal() const noexcept override
                {
                    return viewed_.host_refusal();
                }

                [[nodiscard]] LivenessObserver owner_life() const override
                {
                    return viewed_.owner_life();
                }

                /** @brief Copies on the queue of the source buffer's device. */
                [[nodiscard]] Result<void> read(const ContextHandle& context, GlobalHandle handle,
                                                std::size_t begin, void* destination,
                                                std::size_t bytes) const noexcept override
                {
                    return viewed_.read(context, handle, begin, destination, bytes);
                }

                /** @brief Copies on the queue of the source buffer's device. */
                [[nodiscard]] Result<void> write(const ContextHandle& context, GlobalHandle handle,
                                                 std::size_t begin, const void* source,
                                                 std::size_t bytes) const noexcept override
                {
                    return viewed_.write(context, handle, begin, source, bytes);
                }

                /** @brief Releases nothing: the source buffer owns the handle. */
                void release(const ContextHandle&, GlobalHandle) noexcept override
                {
                }

            private:
                /** @brief The viewed bytes, owning no release. */
                StatedStorage viewed_;
        };
    } // namespace Detail
} // namespace SushiRuntime
