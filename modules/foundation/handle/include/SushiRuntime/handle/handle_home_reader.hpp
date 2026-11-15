/**************************************************************************/
/* handle_home_reader.hpp                                                 */
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
 * @file handle_home_reader.hpp
 * @brief Declares HandleHomeReader, the port that tells where a handle's buffer was made and what it holds.
 * @see modules/foundation/handle/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/handle/element_layout.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_resolver.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    /** @brief Names what is known of the memory a home instance lives in. */
    enum class HomeMemory : std::uint8_t
    {
        Unknown,        /**< The kind of the instance was not recorded; memory the runtime did not allocate. */
        HostAccessible, /**< Every device of the context reads the instance in place. */
        DeviceOnly      /**< Only the device that owns the instance addresses it. */
    };

    /** @brief Holds what a reader sees of the home of one live handle. */
    struct HandleHome
    {
        HandleStatus status = HandleStatus::Ok; /**< Ok, or why nothing was read. */
        MemorySpaceID home;                     /**< Space the buffer was made on. */
        bool resident = false;                  /**< The home space holds an instance. */
        HomeMemory memory = HomeMemory::Unknown; /**< What the home instance lives in. */
        ElementLayout layout;                   /**< Layout of one element of the buffer. */
    };

    /**
     * @brief Resolves handles and says where each buffer was made and how its elements are laid out.
     *
     * A module that must not name the residency directory's own types takes this port.
     */
    class HandleHomeReader : public HandleResolver
    {
        public:
            /** @brief Destroys the reader. */
            ~HandleHomeReader() override = default;

            /** @brief Returns the home of handle; status says why nothing was read when it is not live. */
            [[nodiscard]] virtual HandleHome home_of(GlobalHandle handle) const = 0;

        protected:
            /** @brief Builds the reader. */
            HandleHomeReader() = default;

            /** @brief Copies the reader. */
            HandleHomeReader(const HandleHomeReader&) = default;

            /** @brief Moves the reader. */
            HandleHomeReader(HandleHomeReader&&) = default;

            /** @brief Copies the reader. */
            HandleHomeReader& operator=(const HandleHomeReader&) = default;

            /** @brief Moves the reader. */
            HandleHomeReader& operator=(HandleHomeReader&&) = default;
    };
} // namespace SushiRuntime
