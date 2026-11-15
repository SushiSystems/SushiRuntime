/**************************************************************************/
/* runtime_handle_parts.hpp                                               */
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
 * @file runtime_handle_parts.hpp
 * @brief Declares RuntimeHandleParts, the handle table of a runtime and what is built on it.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/memory/borrowed_index.hpp>
#include <SushiRuntime/memory/coherence_horizon.hpp>
#include <SushiRuntime/memory/handle_ports.hpp>
#include <SushiRuntime/memory/host_write_table.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Owns the residency directory of one runtime, its host-write table, the index of its Borrowed handles and
         *        the horizon that retires its coherence history.
         *
         * Only this unit names ResidencyDirectory; everything else takes ports().
         */
        class RuntimeHandleParts final
        {
            public:
                /** @brief Builds the table in universe, the one every handle of the runtime carries. */
                explicit RuntimeHandleParts(std::uint32_t universe);

                RuntimeHandleParts(const RuntimeHandleParts&) = delete;
                RuntimeHandleParts& operator=(const RuntimeHandleParts&) = delete;
                RuntimeHandleParts(RuntimeHandleParts&&) = delete;
                RuntimeHandleParts& operator=(RuntimeHandleParts&&) = delete;

                /** @brief Returns the ports of the table; valid while this object lives. */
                [[nodiscard]] Memory::HandlePorts ports() noexcept;

                /** @brief Returns how many memory space ids a handle can name. */
                [[nodiscard]] static constexpr std::uint32_t max_memory_spaces() noexcept
                {
                    return Memory::ResidencyDirectory::K_MAX_MEMORY_SPACES;
                }

                /** @brief Returns the index that gives one Borrowed handle per range of caller-owned memory. */
                [[nodiscard]] Memory::BorrowedIndex& borrowed() noexcept;

                /** @brief Returns the horizon the step drivers retire the coherence history at. */
                [[nodiscard]] Memory::CoherenceHorizon& horizon() noexcept;

            private:
                /// @brief Mints, retires and resolves the handles; declared first, destroyed last.
                Memory::ResidencyDirectory directory_;

                /// @brief Records the host writes; reads the directory for liveness, so declared after it.
                Memory::HostWriteTable host_writes_;

                /// @brief Gives the Borrowed handles of caller-owned memory; mints in directory_.
                Memory::BorrowedIndex borrowed_;

                /// @brief Counts the steps of every run and retires the history every interval.
                Memory::CoherenceHorizon horizon_;
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
