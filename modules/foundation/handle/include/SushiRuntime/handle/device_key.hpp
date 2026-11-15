/**************************************************************************/
/* device_key.hpp                                                         */
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
 * @file device_key.hpp
 * @brief Declares DeviceKey, the name of one physical device that holds across runs.
 * @see docs/design/GLOBAL_HANDLE/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace SushiRuntime
{
    /** @brief Names the backend that reports a device. */
    enum class DeviceBackend : std::uint8_t
    {
        Host = 0,      /**< The host processor. */
        LevelZero = 1, /**< Level Zero. */
        CUDA = 2,      /**< CUDA. */
        HIP = 3,       /**< HIP. */
        OpenCL = 4     /**< OpenCL. */
    };

    /** @brief Names what the hardware id bytes of a DeviceKey hold. */
    enum class HardwareIDKind : std::uint8_t
    {
        None = 0,      /**< The backend reports no id; the ordinal names the device. */
        UUID = 1,      /**< A 16-byte device UUID. */
        PCIAddress = 2 /**< A PCI address: domain in bytes 0..3, then bus, device, function. */
    };

    /** @brief Holds the 16 bytes of a hardware id. */
    using HardwareID = std::array<std::uint8_t, 16>;

    /** @brief Names one physical device across runs: backend, hardware id and ordinal. */
    struct DeviceKey
    {
        /// @brief Backend that reports the device.
        DeviceBackend backend = DeviceBackend::Host;

        /// @brief What hardware_id holds.
        HardwareIDKind id_kind = HardwareIDKind::None;

        /// @brief The id; all zero when id_kind is None.
        HardwareID hardware_id{};

        /// @brief Stable ordinal that tells apart devices with no id or with equal ids.
        std::uint32_t ordinal = 0;

        /** @brief Returns whether every field equals other's. */
        [[nodiscard]] bool operator==(const DeviceKey& other) const noexcept
        {
            return backend == other.backend && id_kind == other.id_kind &&
                   hardware_id == other.hardware_id && ordinal == other.ordinal;
        }

        /** @brief Returns whether any field differs from other's. */
        [[nodiscard]] bool operator!=(const DeviceKey& other) const noexcept
        {
            return !(*this == other);
        }

        /** @brief Returns whether this key orders before other: backend, id kind, id, ordinal. */
        [[nodiscard]] bool operator<(const DeviceKey& other) const noexcept
        {
            if (backend != other.backend)
                return backend < other.backend;
            if (id_kind != other.id_kind)
                return id_kind < other.id_kind;
            if (hardware_id != other.hardware_id)
                return hardware_id < other.hardware_id;
            return ordinal < other.ordinal;
        }

        /** @brief Returns whether other orders before this key. */
        [[nodiscard]] bool operator>(const DeviceKey& other) const noexcept
        {
            return other < *this;
        }

        /** @brief Returns whether this key does not order after other. */
        [[nodiscard]] bool operator<=(const DeviceKey& other) const noexcept
        {
            return !(other < *this);
        }

        /** @brief Returns whether this key does not order before other. */
        [[nodiscard]] bool operator>=(const DeviceKey& other) const noexcept
        {
            return !(*this < other);
        }
    };

    static_assert(std::is_trivially_copyable<DeviceKey>::value, "a DeviceKey is copied as bytes");

    namespace Detail
    {
        /** @brief Holds the text name of each backend, indexed by its value. */
        constexpr std::array<std::string_view, 5> K_DEVICE_BACKEND_NAMES = {
            "host", "level_zero", "cuda", "hip", "opencl"};

        /** @brief Holds the text name of each hardware id kind, indexed by its value. */
        constexpr std::array<std::string_view, 3> K_HARDWARE_ID_KIND_NAMES = {"none", "uuid",
                                                                               "pci"};

        /** @brief Holds the lower-case hexadecimal digits. */
        constexpr std::string_view K_HEX_DIGITS = "0123456789abcdef";

        /** @brief Returns the index of name in names, or nothing. */
        template <std::size_t Count>
        [[nodiscard]] std::optional<std::uint8_t>
        find_name(const std::array<std::string_view, Count>& names, std::string_view name) noexcept
        {
            for (std::size_t index = 0; index < Count; ++index)
            {
                if (names[index] == name)
                    return static_cast<std::uint8_t>(index);
            }
            return std::nullopt;
        }

        /** @brief Returns the value of one lower-case hexadecimal digit, or nothing. */
        [[nodiscard]] inline std::optional<std::uint8_t> hex_value(char digit) noexcept
        {
            const std::size_t position = K_HEX_DIGITS.find(digit);
            if (position == std::string_view::npos)
                return std::nullopt;
            return static_cast<std::uint8_t>(position);
        }

        /** @brief Returns the text before the first ':' and drops it and the ':' from text. */
        [[nodiscard]] inline std::string_view take_field(std::string_view& text) noexcept
        {
            const std::size_t colon = text.find(':');
            const std::string_view field = text.substr(0, colon);
            text = colon == std::string_view::npos ? std::string_view() : text.substr(colon + 1);
            return field;
        }
    } // namespace Detail

    /**
     * @brief Returns the stable text form of key: `backend:kind:32 hex digits:ordinal`.
     *
     * Example: `cuda:uuid:00112233445566778899aabbccddeeff:0`.
     */
    [[nodiscard]] inline std::string to_text(const DeviceKey& key)
    {
        std::string text;
        text.reserve(64);
        text += Detail::K_DEVICE_BACKEND_NAMES[static_cast<std::size_t>(key.backend)];
        text += ':';
        text += Detail::K_HARDWARE_ID_KIND_NAMES[static_cast<std::size_t>(key.id_kind)];
        text += ':';
        for (const std::uint8_t byte : key.hardware_id)
        {
            text += Detail::K_HEX_DIGITS[byte >> 4];
            text += Detail::K_HEX_DIGITS[byte & 0x0F];
        }
        text += ':';
        text += std::to_string(key.ordinal);
        return text;
    }

    /** @brief Returns the key whose text form is text, or nothing when text is not one. */
    [[nodiscard]] inline std::optional<DeviceKey> parse_device_key(std::string_view text) noexcept
    {
        const std::optional<std::uint8_t> backend =
            Detail::find_name(Detail::K_DEVICE_BACKEND_NAMES, Detail::take_field(text));
        const std::optional<std::uint8_t> id_kind =
            Detail::find_name(Detail::K_HARDWARE_ID_KIND_NAMES, Detail::take_field(text));
        const std::string_view hex = Detail::take_field(text);
        const std::string_view ordinal = text;
        if (!backend || !id_kind || hex.size() != 32 || ordinal.empty() || ordinal.size() > 10)
            return std::nullopt;

        DeviceKey key;
        key.backend = static_cast<DeviceBackend>(*backend);
        key.id_kind = static_cast<HardwareIDKind>(*id_kind);
        for (std::size_t index = 0; index < key.hardware_id.size(); ++index)
        {
            const std::optional<std::uint8_t> high = Detail::hex_value(hex[2 * index]);
            const std::optional<std::uint8_t> low = Detail::hex_value(hex[2 * index + 1]);
            if (!high || !low)
                return std::nullopt;
            key.hardware_id[index] = static_cast<std::uint8_t>((*high << 4) | *low);
        }

        std::uint64_t value = 0;
        for (const char digit : ordinal)
        {
            if (digit < '0' || digit > '9')
                return std::nullopt;
            value = value * 10 + static_cast<std::uint64_t>(digit - '0');
        }
        if (value > UINT32_MAX)
            return std::nullopt;
        key.ordinal = static_cast<std::uint32_t>(value);
        return key;
    }
} // namespace SushiRuntime

namespace std
{
    /** @brief Hashes a DeviceKey with the same result on every platform and run. */
    template <>
    struct hash<SushiRuntime::DeviceKey>
    {
        /** @brief Returns the 64-bit FNV-1a hash of the key's fields. */
        [[nodiscard]] std::size_t operator()(const SushiRuntime::DeviceKey& key) const noexcept
        {
            std::uint64_t value = 0xCBF29CE484222325ULL;
            const auto mix = [&value](std::uint8_t byte) noexcept
            {
                value = (value ^ byte) * 0x100000001B3ULL;
            };
            mix(static_cast<std::uint8_t>(key.backend));
            mix(static_cast<std::uint8_t>(key.id_kind));
            for (const std::uint8_t byte : key.hardware_id)
                mix(byte);
            for (int shift = 0; shift < 32; shift += 8)
                mix(static_cast<std::uint8_t>(key.ordinal >> shift));
            return static_cast<std::size_t>(value);
        }
    };
} // namespace std
