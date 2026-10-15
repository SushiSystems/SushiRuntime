/**************************************************************************/
/* test_hmac_sha256.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_hmac_sha256.cpp
 * @brief Tests HMAC-SHA256 against RFC 4231 and its constant-time comparison.
 * @see tests/extension/link_security/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>

#include <SushiRuntime/link_security/hmac_sha256.hpp>
#include <SushiRuntime/link_security/sha256.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns the bytes of a text as a span, without its terminator. */
    span<const std::uint8_t> bytes_of(std::string_view text)
    {
        return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
    }

    /** @brief Returns a vector's bytes as a span. */
    span<const std::uint8_t> bytes_of(const std::vector<std::uint8_t>& bytes)
    {
        return { bytes.data(), bytes.size() };
    }

    /** @brief Returns a digest as 64 lowercase hexadecimal digits. */
    std::string to_hex(const Sha256Digest& digest)
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

    /** @brief Returns @p count bytes that all hold @p value. */
    std::vector<std::uint8_t> repeated(std::uint8_t value, std::size_t count)
    {
        return std::vector<std::uint8_t>(count, value);
    }

    /** @brief Returns the bytes first, first + 1, ... of the given count. */
    std::vector<std::uint8_t> counting(std::uint8_t first, std::size_t count)
    {
        std::vector<std::uint8_t> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::uint8_t>(first + index);
        return bytes;
    }
}

/** @brief Tests HMAC-SHA256 against test cases 1, 2, 3, 4, 6 and 7 of RFC 4231. */
TEST(Medium_HmacSha256Test, HmacRfc4231Vectors)
{
    EXPECT_EQ(to_hex(hmac_sha256(bytes_of(repeated(0x0b, 20)), bytes_of("Hi There"))),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    EXPECT_EQ(to_hex(hmac_sha256(bytes_of("Jefe"), bytes_of("what do ya want for nothing?"))),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    EXPECT_EQ(to_hex(hmac_sha256(bytes_of(repeated(0xaa, 20)), bytes_of(repeated(0xdd, 50)))),
              "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    EXPECT_EQ(to_hex(hmac_sha256(bytes_of(counting(0x01, 25)), bytes_of(repeated(0xcd, 50)))),
              "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    EXPECT_EQ(to_hex(hmac_sha256(
                  bytes_of(repeated(0xaa, 131)),
                  bytes_of("Test Using Larger Than Block-Size Key - Hash Key First"))),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    EXPECT_EQ(to_hex(hmac_sha256(
                  bytes_of(repeated(0xaa, 131)),
                  bytes_of("This is a test using a larger than block-size key and a larger "
                           "than block-size data. The key needs to be hashed before being "
                           "used by the HMAC algorithm."))),
              "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

/** @brief Tests that the constant-time comparison sees a difference in any byte. */
TEST(Medium_HmacSha256Test, EqualInConstantTimeSeesEveryByte)
{
    const Sha256Digest digest = sha256(bytes_of("abc"));
    EXPECT_TRUE(equal_in_constant_time(digest, digest));

    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        Sha256Digest altered = digest;
        altered[index] ^= 0x01u;
        EXPECT_FALSE(equal_in_constant_time(digest, altered)) << "byte " << index;
    }
}
