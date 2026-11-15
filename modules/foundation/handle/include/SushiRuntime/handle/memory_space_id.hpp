/**************************************************************************/
/* memory_space_id.hpp                                                    */
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
 * @file memory_space_id.hpp
 * @brief Declares MemorySpaceID, the opaque name of one place a buffer instance can live.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace SushiRuntime
{
    /** @brief Names one memory space; the value means nothing outside the runtime that gave it. */
    class MemorySpaceID final
    {
        public:
            /** @brief Builds the id with value 0. */
            constexpr MemorySpaceID() noexcept = default;

            /** @brief Builds the id with the given value. */
            constexpr explicit MemorySpaceID(std::uint32_t value) noexcept : value_(value)
            {

            }

            /** @brief Returns the raw value. */
            [[nodiscard]] constexpr std::uint32_t value() const noexcept
            {
                return value_;
            }

            /** @brief Returns whether both ids hold the same value. */
            [[nodiscard]] constexpr bool operator==(MemorySpaceID other) const noexcept
            {
                return value_ == other.value_;
            }

            /** @brief Returns whether the ids hold different values. */
            [[nodiscard]] constexpr bool operator!=(MemorySpaceID other) const noexcept
            {
                return value_ != other.value_;
            }

            /** @brief Returns whether this id's value is below other's. */
            [[nodiscard]] constexpr bool operator<(MemorySpaceID other) const noexcept
            {
                return value_ < other.value_;
            }

        private:
            /// @brief Raw value.
            std::uint32_t value_ = 0;
    };

    static_assert(std::is_trivially_copyable<MemorySpaceID>::value,
                  "a MemorySpaceID is copied as bytes");
} // namespace SushiRuntime

namespace std
{
    /** @brief Hashes a MemorySpaceID by its value. */
    template <>
    struct hash<SushiRuntime::MemorySpaceID>
    {
        /** @brief Returns the value as the hash. */
        [[nodiscard]] std::size_t operator()(SushiRuntime::MemorySpaceID id) const noexcept
        {
            return static_cast<std::size_t>(id.value());
        }
    };
} // namespace std
