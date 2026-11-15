/**************************************************************************/
/* device_contexts.hpp                                                    */
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
 * @file device_contexts.hpp
 * @brief Declares DeviceContexts, the port that counts devices and says which of them share a memory context.
 * @see modules/foundation/handle/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

namespace SushiRuntime
{
    /**
     * @brief Counts the devices of a runtime and says which of them share one memory context.
     *
     * A module that must not name the USM allocator takes this port; the allocator implements it.
     */
    class DeviceContexts
    {
        public:
            /** @brief Destroys the port. */
            virtual ~DeviceContexts() = default;

            /** @brief Returns how many devices there are. */
            [[nodiscard]] virtual std::size_t device_count() const noexcept = 0;

            /**
             * @brief Tells whether two devices share one memory context.
             * @pre Both indices are below device_count().
             */
            [[nodiscard]] virtual bool shares_context(std::size_t first, std::size_t second) const = 0;

        protected:
            /** @brief Builds the port. */
            DeviceContexts() = default;

            /** @brief Copies the port. */
            DeviceContexts(const DeviceContexts&) = default;

            /** @brief Moves the port. */
            DeviceContexts(DeviceContexts&&) = default;

            /** @brief Copies the port. */
            DeviceContexts& operator=(const DeviceContexts&) = default;

            /** @brief Moves the port. */
            DeviceContexts& operator=(DeviceContexts&&) = default;
    };
} // namespace SushiRuntime
