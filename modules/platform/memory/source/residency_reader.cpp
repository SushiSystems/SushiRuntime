/**************************************************************************/
/* residency_reader.cpp                                                   */
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
 * @file residency_reader.cpp
 * @brief Defines ResidencyReader::home_of(), the home of a handle read from inspect().
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/residency_reader.hpp>

#include <limits>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        namespace
        {
            /** @brief Spaces EntryView::valid_spaces has one bit for. */
            constexpr std::uint32_t K_VALID_SPACE_BITS = std::numeric_limits<std::uint64_t>::digits;

            /** @brief Returns what the kind of an instance says about who can read it in place. */
            HomeMemory memory_of(const std::optional<USMKind>& kind) noexcept
            {
                if (!kind)
                    return HomeMemory::Unknown;
                return *kind == USMKind::Device ? HomeMemory::DeviceOnly : HomeMemory::HostAccessible;
            }
        } // namespace

        HandleHome ResidencyReader::home_of(GlobalHandle handle) const
        {
            const EntryView entry = inspect(handle);
            HandleHome home;
            home.status = entry.status;
            home.home = entry.home;
            home.layout = entry.layout;
            home.memory = memory_of(entry.home_kind);
            home.resident = entry.status == HandleStatus::Ok && entry.home.value() < K_VALID_SPACE_BITS &&
                            ((entry.valid_spaces >> entry.home.value()) & 1U) != 0U;
            return home;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
