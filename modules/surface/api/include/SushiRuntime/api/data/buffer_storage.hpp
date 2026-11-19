/**************************************************************************/
/* buffer_storage.hpp                                                     */
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
 * @file buffer_storage.hpp
 * @brief Declares BufferStorage, the one interface behind which a Buffer keeps its memory.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <optional>

#include <SushiRuntime/api/data/residency.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/strong_id.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/region/liveness.hpp>

namespace SushiRuntime
{
    /**
     * @brief Which device queue an allocation is pinned to.
     *
     * Its constructor is explicit, so a bare integer does not pass as a device index.
     */
    using DeviceIndex = StrongID<struct DeviceIndexTag>;

    namespace Detail
    {
        /**
         * @brief Holds the one allocation a Buffer views, with nothing of how it was made.
         *        Move-only.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        struct MemoryBlock
        {
            Allocation allocation; /**< The memory; owns no release for borrowed memory. */
        };

        /**
         * @brief Keeps the memory of one Buffer and moves its bytes to and from the host.
         *
         * StatedStorage keeps memory on one stated device. PlannedStorage keeps none: the
         * runtime places the instances. AliasStorage views another live buffer's memory. Every storage answers every query below.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        class SUSHIRUNTIME_API BufferStorage
        {
            public:
                virtual ~BufferStorage() = default;

                /**
                 * @brief Returns the address of the buffer's instance on its stated device.
                 * @return The first byte, or null when the storage holds no memory of its own.
                 */
                [[nodiscard]] virtual void* address() const noexcept = 0;

                /**
                 * @brief Returns the device the memory is pinned to.
                 * @return The device, or empty when the storage states none.
                 */
                [[nodiscard]] virtual std::optional<DeviceIndex> device_index() const noexcept = 0;

                /** @brief Returns where the memory lives. */
                [[nodiscard]] virtual Residency residency() const noexcept = 0;

                /**
                 * @brief Tells whether the host may index the memory directly.
                 * @return True for shared and host USM.
                 */
                [[nodiscard]] virtual bool host_addressable() const noexcept = 0;

                /**
                 * @brief Returns the opening of the message that refuses a direct host index.
                 * @return A sentence ending in a semicolon and a space.
                 */
                [[nodiscard]] virtual const char* host_refusal() const noexcept = 0;

                /**
                 * @brief Returns the observer of the owner of borrowed memory.
                 * @return The owner's observer, or an empty one for memory the buffer owns.
                 */
                [[nodiscard]] virtual LivenessObserver owner_life() const = 0;

                /**
                 * @brief Copies bytes of the buffer to host memory and waits.
                 * @param handle The buffer's handle.
                 * @param begin First byte of the buffer to read.
                 * @return Nothing, or the error the copy failed with.
                 * @pre begin + bytes lies inside the buffer.
                 */
                [[nodiscard]] virtual Result<void> read(const ContextHandle& context,
                                                        GlobalHandle handle, std::size_t begin,
                                                        void* destination,
                                                        std::size_t bytes) const noexcept = 0;

                /**
                 * @brief Copies host bytes into the buffer and waits.
                 * @param handle The buffer's handle.
                 * @param begin First byte of the buffer to write.
                 * @return Nothing, or the error the copy failed with.
                 * @pre begin + bytes lies inside the buffer.
                 */
                [[nodiscard]] virtual Result<void> write(const ContextHandle& context,
                                                         GlobalHandle handle, std::size_t begin,
                                                         const void* source,
                                                         std::size_t bytes) const noexcept = 0;

                /**
                 * @brief Releases the buffer's handle and every instance it records.
                 * @param handle The buffer's handle; null releases nothing.
                 */
                virtual void release(const ContextHandle& context,
                                     GlobalHandle handle) noexcept = 0;
        };
    } // namespace Detail
} // namespace SushiRuntime
