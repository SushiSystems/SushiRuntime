/**************************************************************************/
/* handle_status.hpp                                                      */
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
 * @file handle_status.hpp
 * @brief Declares HandleStatus, the outcome of minting, retiring or resolving a handle.
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /** @brief Names the outcome of an operation on a GlobalHandle. */
    enum class HandleStatus : std::uint8_t
    {
        Ok = 0,              /**< The operation succeeded. */
        NullHandle = 1,      /**< The handle is the null handle. */
        StaleHandle = 2,     /**< The slot was retired or reused since the handle was minted. */
        ForeignUniverse = 3, /**< The handle was minted by another runtime instance. */
        NotResident = 4,     /**< The handle has no instance on the asked memory space. */
        SpaceExhausted = 5   /**< No slot is left to mint a handle in. */
    };
} // namespace SushiRuntime
