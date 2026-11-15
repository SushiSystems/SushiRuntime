/**************************************************************************/
/* planned_storage.hpp                                                    */
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
 * @file planned_storage.hpp
 * @brief Declares PlannedStorage, the storage of a buffer the runtime places.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <optional>

#include <SushiRuntime/api/data/buffer_storage.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Keeps no memory: the runtime's directory holds the instances, and the host
         *        reaches the bytes through it.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        class SUSHIRUNTIME_API PlannedStorage final : public BufferStorage
        {
            public:
                [[nodiscard]] void* address() const noexcept override { return nullptr; }

                [[nodiscard]] std::optional<DeviceIndex> device_index() const noexcept override
                {
                    return std::nullopt;
                }

                [[nodiscard]] Residency residency() const noexcept override
                {
                    return Residency::Planned;
                }

                [[nodiscard]] bool host_addressable() const noexcept override { return false; }

                [[nodiscard]] const char* host_refusal() const noexcept override
                {
                    return "a planned buffer has no address the host may hold; ";
                }

                [[nodiscard]] LivenessObserver owner_life() const override
                {
                    return LivenessObserver();
                }

                /** @brief Reads through the runtime's directory; fails with its error. */
                [[nodiscard]] Result<void> read(const ContextHandle& context, GlobalHandle handle,
                                                std::size_t begin, void* destination,
                                                std::size_t bytes) const noexcept override;

                /** @brief Writes through the runtime's directory; fails with its error. */
                [[nodiscard]] Result<void> write(const ContextHandle& context, GlobalHandle handle,
                                                 std::size_t begin, const void* source,
                                                 std::size_t bytes) const noexcept override;

                /** @brief Frees every instance of the buffer and retires its handle. */
                void release(const ContextHandle& context, GlobalHandle handle) noexcept override;
        };
    } // namespace Detail
} // namespace SushiRuntime
