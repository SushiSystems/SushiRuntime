/**************************************************************************/
/* device_memory_space.hpp                                                */
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
 * @file device_memory_space.hpp
 * @brief Declares DeviceMemorySpace, the memory space of one device.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/memory/memory_probe.hpp>
#include <SushiRuntime/memory/memory_space.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds what a DeviceMemorySpace is built from, besides its probe. */
        struct DeviceMemorySpaceDescription
        {
            MemorySpaceID id;                          /**< Id of the space. */
            std::uint32_t shared_pool = 0;             /**< Id of the pool the space draws on. */
            std::vector<MemorySpaceID> direct_targets; /**< Spaces it copies to without staging. */
        };

        /**
         * @brief The memory space of one device.
         *
         * The reserve is measured once, at construction, as the bytes the probe reports not free.
         * An integrated GPU names the host's pool as its shared pool.
         */
        class DeviceMemorySpace final : public MemorySpace
        {
            public:
                /** @brief Builds the space and measures its capacity and reserve through probe. */
                DeviceMemorySpace(DeviceMemorySpaceDescription description, const MemoryProbe& probe);

                [[nodiscard]] MemorySpaceID id() const noexcept override
                {
                    return id_;
                }

                [[nodiscard]] std::string_view kind_name() const noexcept override
                {
                    return "device";
                }

                [[nodiscard]] std::size_t capacity_bytes() const noexcept override
                {
                    return capacity_bytes_;
                }

                [[nodiscard]] std::size_t reserve_bytes() const noexcept override
                {
                    return reserve_bytes_;
                }

                [[nodiscard]] std::uint32_t shared_pool() const noexcept override
                {
                    return shared_pool_;
                }

                [[nodiscard]] const std::vector<MemorySpaceID>& direct_targets() const noexcept override
                {
                    return direct_targets_;
                }

            private:
                /// @brief Id of the space.
                MemorySpaceID id_;

                /// @brief Id of the pool the space draws on.
                std::uint32_t shared_pool_;

                /// @brief Spaces it copies to without staging, sorted.
                std::vector<MemorySpaceID> direct_targets_;

                /// @brief Total bytes the probe reported.
                std::size_t capacity_bytes_ = 0;

                /// @brief Bytes the probe reported not free at construction.
                std::size_t reserve_bytes_ = 0;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
