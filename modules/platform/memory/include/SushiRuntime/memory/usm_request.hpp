/**************************************************************************/
/* usm_request.hpp                                                        */
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
 * @file usm_request.hpp
 * @brief Declares USMKind and USMRequest, which describe one USM allocation.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Names the three kinds of USM an allocation can be. */
        enum class USMKind : std::uint8_t
        {
            Host,   /**< Host memory a device of the context can read; sycl::usm::alloc::host. */
            Shared, /**< Memory that migrates between host and device; sycl::usm::alloc::shared. */
            Device  /**< Memory only the device addresses; sycl::usm::alloc::device. */
        };

        /**
         * @brief Describes one USM allocation: its kind, size, alignment and device.
         * @see modules/platform/memory/README.md
         */
        struct USMRequest
        {
            /** @brief Kind of USM to allocate. */
            USMKind kind = USMKind::Shared;

            /** @brief Length of the allocation in bytes. */
            std::size_t bytes = 0;

            /** @brief Byte alignment of the returned address; 0 asks for none. */
            std::size_t alignment = 0;

            /** @brief Index of the device whose queue and context own the allocation. */
            std::size_t device_index = 0;
        };

    } // namespace Memory
} // namespace SushiRuntime
