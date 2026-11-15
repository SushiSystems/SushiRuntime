/**************************************************************************/
/* handle_ownership.hpp                                                   */
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
 * @file handle_ownership.hpp
 * @brief Declares HandleOwnership, which decides the owner machine of a shared handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Holds the machine id of the master; the handles it mints carry it. */
        inline constexpr std::uint16_t K_MASTER_MACHINE = 0;

        /**
         * @brief Names what classify_ownership() decided for a handle.
         */
        enum class OwnershipStatus : std::uint8_t
        {
            Owned,   /**< This machine minted the handle; it shares and rebuilds the buffer. */
            Foreign, /**< Another machine minted the handle; its operations stay with that owner. */
            Null,    /**< The null handle has no owner. */
        };

        /**
         * @brief Decides whether the machine @p local_machine owns @p handle.
         *
         * The owner is the handle's machine field, so the answer is a pure function of the two
         * arguments and no table is kept. The caller shares an owned handle and leaves the
         * operations of a foreign one to its owner.
         * @return Owned, Foreign or Null.
         */
        [[nodiscard]] SUSHIRUNTIME_API OwnershipStatus classify_ownership(
            GlobalHandle handle, std::uint16_t local_machine) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
