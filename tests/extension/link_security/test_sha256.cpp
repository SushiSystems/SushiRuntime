/**************************************************************************/
/* test_sha256.cpp                                                        */
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
 * @file test_sha256.cpp
 * @brief Tests SHA-256 against the FIPS 180-4 vectors and in pieces.
 * @see tests/extension/link_security/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>

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
}

/** @brief Tests SHA-256 against the example messages of FIPS 180-4. */
TEST(Medium_Sha256Test, Sha256Fips180Vectors)
{
    EXPECT_EQ(to_hex(sha256(bytes_of(""))),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(to_hex(sha256(bytes_of("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(to_hex(sha256(bytes_of(
                  "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(to_hex(sha256(bytes_of(
                  "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                  "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"))),
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");

    const std::vector<std::uint8_t> million = repeated('a', 1000000);
    EXPECT_EQ(to_hex(sha256(bytes_of(million))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/** @brief Tests that a message fed in uneven pieces hashes like the whole message. */
TEST(Medium_Sha256Test, PiecewiseUpdateMatchesOneShot)
{
    const std::vector<std::uint8_t> million = repeated('a', 1000000);
    const std::size_t piece = 127;

    Sha256 hash;
    for (std::size_t offset = 0; offset < million.size(); offset += piece)
    {
        const std::size_t count = std::min(piece, million.size() - offset);
        hash.update({ million.data() + offset, count });
    }
    EXPECT_EQ(to_hex(hash.finish()),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    hash.update(bytes_of("abc"));
    EXPECT_EQ(to_hex(hash.finish()),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
