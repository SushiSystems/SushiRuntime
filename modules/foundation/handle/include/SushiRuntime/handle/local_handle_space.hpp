/**************************************************************************/
/* local_handle_space.hpp                                                 */
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
 * @file local_handle_space.hpp
 * @brief Declares LocalHandleSpace, a runtime-free minter for memory only its owner uses.
 * @see docs/design/GLOBAL_HANDLE/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_space.hpp>
#include <SushiRuntime/handle/handle_status.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the universe of every handle a LocalHandleSpace mints.
     *
     * No runtime directory is built in this universe, so a runtime refuses these handles.
     */
    inline constexpr std::uint32_t K_LOCAL_HANDLE_UNIVERSE = 0xFFFFFFFFU;

    /**
     * @brief Mints handles for host memory that only its owner uses, with no runtime.
     *
     * Handles carry K_LOCAL_HANDLE_UNIVERSE, the space id in the machine field, generation 1
     * and the flags HostOnly and Borrowed. Slots count up from 0, so the same mints give the
     * same handles. Spaces of one owner need different ids. A handle is never retired.
     */
    class LocalHandleSpace final
    {
        public:
            /** @brief Builds a space whose handles carry space_id in the machine field. */
            explicit constexpr LocalHandleSpace(std::uint16_t space_id) noexcept
                : space_id_(space_id)
            {

            }

            /** @brief Copies are forbidden: two spaces would mint the same handles. */
            LocalHandleSpace(const LocalHandleSpace&) = delete;

            /** @brief Moves are forbidden: the counter is shared by every minting thread. */
            LocalHandleSpace(LocalHandleSpace&&) = delete;

            /** @brief Copies are forbidden. */
            LocalHandleSpace& operator=(const LocalHandleSpace&) = delete;

            /** @brief Moves are forbidden. */
            LocalHandleSpace& operator=(LocalHandleSpace&&) = delete;

            /** @brief Destroys the space. */
            ~LocalHandleSpace() = default;

            /**
             * @brief Mints the next handle of the given kind.
             * @return Ok with the handle, or SpaceExhausted with the null handle once 2^32
             *         slots are used.
             */
            [[nodiscard]] MintOutcome mint(HandleKind kind) noexcept
            {
                const std::uint64_t slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
                if (slot > K_LAST_SLOT)
                    return MintOutcome{GlobalHandle::null(), HandleStatus::SpaceExhausted};
                return MintOutcome{GlobalHandle(K_LOCAL_HANDLE_UNIVERSE, space_id_, kind,
                                                HandleFlags::HostOnly | HandleFlags::Borrowed,
                                                K_GENERATION, static_cast<std::uint32_t>(slot)),
                                   HandleStatus::Ok};
            }

            /** @brief Returns the id the caller gave at construction. */
            [[nodiscard]] constexpr std::uint16_t space_id() const noexcept
            {
                return space_id_;
            }

            /** @brief Returns whether handle was minted in the local universe. */
            [[nodiscard]] static constexpr bool is_local(GlobalHandle handle) noexcept
            {
                return handle.universe() == K_LOCAL_HANDLE_UNIVERSE;
            }

        private:
            /// @brief Highest slot a handle can name.
            static constexpr std::uint64_t K_LAST_SLOT = 0xFFFFFFFFULL;

            /// @brief Generation of every handle; slots are never reused.
            static constexpr std::uint32_t K_GENERATION = 1;

            /// @brief Id carried in the machine field.
            std::uint16_t space_id_;

            /// @brief Slot the next mint takes.
            std::atomic<std::uint64_t> next_slot_{0};
    };
} // namespace SushiRuntime
