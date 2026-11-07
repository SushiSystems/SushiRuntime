/**************************************************************************/
/* digest_bytes.hpp                                                       */
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
 * @file digest_bytes.hpp
 * @brief Views text and byte vectors as byte spans and writes a SHA-256 digest as hexadecimal text.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <SushiRuntime/core/span.hpp>

#include <SushiRuntime/link_security/sha256.hpp>

namespace TestSupport
{

/** @brief Returns the bytes of @p text as a span, without its terminator. */
[[nodiscard]] inline SushiRuntime::span<const std::uint8_t> bytes_of(std::string_view text)
{
    return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
}

/** @brief Returns the bytes of @p bytes as a span. */
[[nodiscard]] inline SushiRuntime::span<const std::uint8_t> bytes_of(const std::vector<std::uint8_t>& bytes)
{
    return { bytes.data(), bytes.size() };
}

/** @brief Returns @p digest as 64 lowercase hexadecimal digits. */
[[nodiscard]] inline std::string to_hex(const SushiRuntime::Distributed::Sha256Digest& digest)
{
    static constexpr char K_DIGITS[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest)
    {
        text.push_back(K_DIGITS[byte >> 4]);
        text.push_back(K_DIGITS[byte & 0x0fu]);
    }
    return text;
}

} // namespace TestSupport
