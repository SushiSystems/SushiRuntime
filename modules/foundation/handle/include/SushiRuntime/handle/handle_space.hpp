/**************************************************************************/
/* handle_space.hpp                                                       */
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
 * @file handle_space.hpp
 * @brief Declares HandleSpace, the interface that mints and retires global handles.
 * @see docs/design/GLOBAL_HANDLE/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>

namespace SushiRuntime
{
    /** @brief Holds a minted handle, or the null handle with the reason none was minted. */
    struct MintOutcome
    {
        GlobalHandle handle;                     /**< The handle; null unless status is Ok. */
        HandleStatus status = HandleStatus::Ok; /**< Ok, or why no handle was minted. */
    };

    /** @brief Mints handles in recording order and retires them. */
    class HandleSpace
    {
        public:
            /** @brief Destroys the space. */
            virtual ~HandleSpace() = default;

            /** @brief Mints a handle of the given kind carrying the given flags. */
            [[nodiscard]] virtual MintOutcome mint(HandleKind kind, HandleFlags flags) = 0;

            /**
             * @brief Retires handle so its slot can be reused under a higher generation.
             * @return Ok, or NullHandle, StaleHandle or ForeignUniverse.
             */
            [[nodiscard]] virtual HandleStatus retire(GlobalHandle handle) = 0;

        protected:
            /** @brief Builds the space. */
            HandleSpace() = default;

            /** @brief Copies the space. */
            HandleSpace(const HandleSpace&) = default;

            /** @brief Moves the space. */
            HandleSpace(HandleSpace&&) = default;

            /** @brief Copies the space. */
            HandleSpace& operator=(const HandleSpace&) = default;

            /** @brief Moves the space. */
            HandleSpace& operator=(HandleSpace&&) = default;
    };
} // namespace SushiRuntime
