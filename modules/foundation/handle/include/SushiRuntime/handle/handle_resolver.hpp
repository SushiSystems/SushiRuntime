/**************************************************************************/
/* handle_resolver.hpp                                                    */
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
 * @file handle_resolver.hpp
 * @brief Declares HandleResolver, the interface that turns a handle into a local address.
 * @see docs/design/GLOBAL_HANDLE/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>

namespace SushiRuntime
{
    /** @brief Holds a resolved local address, or a null address with the reason. */
    struct ResolveOutcome
    {
        void* address = nullptr;                 /**< The address; null unless status is Ok. */
        HandleStatus status = HandleStatus::Ok; /**< Ok, or why the handle did not resolve. */
    };

    /** @brief Turns a handle into the address of its instance on one memory space. */
    class HandleResolver
    {
        public:
            /** @brief Destroys the resolver. */
            virtual ~HandleResolver() = default;

            /**
             * @brief Resolves handle to its instance on space.
             * @return Ok with the address, or NullHandle, StaleHandle, ForeignUniverse or
             *         NotResident with a null address.
             */
            [[nodiscard]] virtual ResolveOutcome resolve(GlobalHandle handle,
                                                         MemorySpaceID space) const = 0;

            /** @brief Returns whether handle's slot was retired or reused since it was minted. */
            [[nodiscard]] virtual bool is_stale(GlobalHandle handle) const = 0;

        protected:
            /** @brief Builds the resolver. */
            HandleResolver() = default;

            /** @brief Copies the resolver. */
            HandleResolver(const HandleResolver&) = default;

            /** @brief Moves the resolver. */
            HandleResolver(HandleResolver&&) = default;

            /** @brief Copies the resolver. */
            HandleResolver& operator=(const HandleResolver&) = default;

            /** @brief Moves the resolver. */
            HandleResolver& operator=(HandleResolver&&) = default;
    };
} // namespace SushiRuntime
