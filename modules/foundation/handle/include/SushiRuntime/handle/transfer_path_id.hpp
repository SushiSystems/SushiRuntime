/**************************************************************************/
/* transfer_path_id.hpp                                                   */
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

/**
 * @file transfer_path_id.hpp
 * @brief Declares TransferPathID, the stable identity of one way of copying between memory spaces.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>

namespace SushiRuntime
{
    /**
     * @brief Names one transfer path by a value that is the same on every run and every machine.
     *
     * A path class builds its id once from its own name with named(). Code that selects, prices or
     * records a path compares ids. A name is kept only for logs and for the keys of the profile file,
     * which map back to an id once, when the file is read.
     */
    class TransferPathID final
    {
        public:
            /** @brief Builds the id that names no path. */
            constexpr TransferPathID() noexcept = default;

            /** @brief Returns the id of the path called name; the same name gives the same id. */
            [[nodiscard]] static constexpr TransferPathID named(std::string_view name) noexcept
            {
                std::uint64_t value = K_OFFSET_BASIS;
                for (const char letter : name)
                    value = (value ^ static_cast<unsigned char>(letter)) * K_PRIME;
                return TransferPathID(value == 0 ? 1 : value);
            }

            /** @brief Returns the raw value; 0 means no path. */
            [[nodiscard]] constexpr std::uint64_t value() const noexcept
            {
                return value_;
            }

            /** @brief Returns whether this id names a path. */
            [[nodiscard]] constexpr bool is_set() const noexcept
            {
                return value_ != 0;
            }

            /** @brief Returns whether both ids hold the same value. */
            [[nodiscard]] constexpr bool operator==(TransferPathID other) const noexcept
            {
                return value_ == other.value_;
            }

            /** @brief Returns whether the ids hold different values. */
            [[nodiscard]] constexpr bool operator!=(TransferPathID other) const noexcept
            {
                return value_ != other.value_;
            }

            /** @brief Returns whether this id's value is below other's. */
            [[nodiscard]] constexpr bool operator<(TransferPathID other) const noexcept
            {
                return value_ < other.value_;
            }

        private:
            /** @brief Builds the id with the given value. */
            constexpr explicit TransferPathID(std::uint64_t value) noexcept : value_(value)
            {

            }

            /// @brief FNV-1a 64-bit offset basis.
            static constexpr std::uint64_t K_OFFSET_BASIS = 14695981039346656037ULL;

            /// @brief FNV-1a 64-bit prime.
            static constexpr std::uint64_t K_PRIME = 1099511628211ULL;

            /// @brief Raw value.
            std::uint64_t value_ = 0;
    };

    static_assert(std::is_trivially_copyable<TransferPathID>::value,
                  "a TransferPathID is copied as bytes");
} // namespace SushiRuntime

namespace std
{
    /** @brief Hashes a TransferPathID by its value. */
    template <>
    struct hash<SushiRuntime::TransferPathID>
    {
        /** @brief Returns the value as the hash. */
        [[nodiscard]] std::size_t operator()(SushiRuntime::TransferPathID id) const noexcept
        {
            return static_cast<std::size_t>(id.value());
        }
    };
} // namespace std
