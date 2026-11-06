/**************************************************************************/
/* determinism.hpp                                                        */
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
 * @file determinism.hpp
 * @brief Declares Determinism, the determinism contract a node's recorder states for its result.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    /**
     * @brief Names the determinism contract a recorder states for a node's result.
     *
     * The runtime carries the contract and reports it; it decides nothing by it.
     * @see modules/foundation/core/README.md
     */
    enum class Determinism : std::uint8_t
    {
        Unstated, /**< The recorder stated nothing. */
        Bitwise,  /**< The result repeats bit for bit on the same hardware. */
        Tolerant, /**< The result agrees within a tolerance the recorder states. */
        Cosmetic, /**< The result only has to look right. */
    };
} // namespace SushiRuntime
