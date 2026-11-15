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
 * @brief Declares HandleOwnership, which records the owner machine of each shared handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Holds the machine id of the master; the handles it mints carry it. */
        inline constexpr std::uint16_t K_MASTER_MACHINE = 0;

        /**
         * @brief Names what HandleOwnership::admit() decided for a handle.
         */
        enum class OwnershipStatus : std::uint8_t
        {
            Owned,   /**< This machine minted the handle; it shares and rebuilds the buffer. */
            Foreign, /**< Another machine minted the handle; its operations stay with that owner. */
            Null,    /**< The null handle has no owner. */
        };

        /**
         * @brief Records, per handle, the one machine that owns it: the machine that minted it.
         *
         * The owner is the handle's machine field, so the answer is a pure function of the
         * handle. Only an owned handle is recorded; a foreign one is refused, and the caller
         * leaves its operations to the owner. One mutex covers the table.
         */
        class SUSHIRUNTIME_API HandleOwnership final
        {
            public:
                /** @brief Builds an empty table for the machine @p local_machine. */
                explicit HandleOwnership(std::uint16_t local_machine) noexcept;

                HandleOwnership(const HandleOwnership&) = delete;
                HandleOwnership& operator=(const HandleOwnership&) = delete;
                HandleOwnership(HandleOwnership&&) = delete;
                HandleOwnership& operator=(HandleOwnership&&) = delete;

                /**
                 * @brief Records a handle this machine owns, or refuses one it does not.
                 * @return Owned when recorded; Foreign or Null when refused and not recorded.
                 */
                [[nodiscard]] OwnershipStatus admit(GlobalHandle handle);

                /** @brief Forgets a handle; one never admitted changes nothing. */
                void forget(GlobalHandle handle) noexcept;

                /** @brief Returns the owner machine of a recorded handle, or nothing. */
                [[nodiscard]] std::optional<std::uint16_t> owner_of(GlobalHandle handle) const;

                /** @brief Returns the machine id this table admits handles for. */
                [[nodiscard]] std::uint16_t local_machine() const noexcept;

            private:
                /// @brief The machine this table runs on.
                std::uint16_t local_machine_;

                /// @brief Guards the table.
                mutable std::mutex mutex_;

                /// @brief Owner machine by handle, in handle order.
                std::map<GlobalHandle, std::uint16_t> owners_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
