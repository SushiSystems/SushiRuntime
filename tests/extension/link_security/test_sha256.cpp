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
 * @file test_sha256.cpp
 * @brief Tests Sha256: the FIPS 180-4 example messages, uneven updates and the reset after finish().
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

#include "support/digest_bytes.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using TestSupport::bytes_of;
using TestSupport::to_hex;

namespace
{
    /** @brief Returns @p count bytes that all hold @p value. */
    std::vector<std::uint8_t> repeated(std::uint8_t value, std::size_t count)
    {
        return std::vector<std::uint8_t>(count, value);
    }

    /** @brief Returns the digest of a message fed to one Sha256 in pieces of the given size. */
    Sha256Digest hash_in_pieces(const std::vector<std::uint8_t>& message, std::size_t piece)
    {
        Sha256 hash;
        for (std::size_t offset = 0; offset < message.size(); offset += piece)
        {
            const std::size_t count = std::min(piece, message.size() - offset);
            hash.update({ message.data() + offset, count });
        }
        return hash.finish();
    }

    /** @brief Holds a message, built by repeating a unit, and the digest FIPS 180-4 gives it. */
    struct DigestRow
    {
        const char* meaning;   /**< Names the message in a test report. */
        std::string unit;      /**< Text repeated to build the message. */
        std::size_t repeat;    /**< How many times the unit is repeated. */
        const char* digest;    /**< The published digest, 64 lowercase hexadecimal digits. */
    };

    /** @brief Names a row by its meaning in a test report. */
    std::string name_of(const ::testing::TestParamInfo<DigestRow>& info)
    {
        return info.param.meaning;
    }

    /** @brief Holds the example messages of FIPS 180-4 and the digests the standard publishes. */
    const DigestRow K_FIPS_MESSAGES[] = {
        {"EmptyMessage", "", 1,
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"Abc", "abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"Message448Bits", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"Message896Bits",
         "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrl"
         "mnopqrsmnopqrstnopqrstu",
         1, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
        {"OneMillionA", "a", 1000000,
         "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };

    /** @brief Holds the digest of one million times 'a', published in FIPS 180-4. */
    constexpr char K_MILLION_A_DIGEST[] =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

    /** @brief Holds the digest of "abc", published in FIPS 180-4. */
    constexpr char K_ABC_DIGEST[] =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
} // namespace

/** @brief Runs a test once per example message of FIPS 180-4. */
class Small_Sha256Test : public ::testing::TestWithParam<DigestRow>
{
};

/** @brief Tests SHA-256 against the example messages of FIPS 180-4. */
TEST_P(Small_Sha256Test, Sha256Fips180Vectors)
{
    const DigestRow& row = GetParam();
    std::string message;
    for (std::size_t index = 0; index < row.repeat; ++index)
        message += row.unit;

    EXPECT_EQ(to_hex(sha256(bytes_of(message))), row.digest);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_Sha256Test, ::testing::ValuesIn(K_FIPS_MESSAGES), name_of);

/** @brief Tests that a message fed in uneven pieces hashes like the whole message. */
TEST(Small_Sha256, PiecewiseUpdateMatchesOneShot)
{
    const std::vector<std::uint8_t> million = repeated('a', 1000000);
    const std::size_t piece = 127;

    EXPECT_EQ(to_hex(hash_in_pieces(million, piece)), K_MILLION_A_DIGEST);
}

/** @brief Tests that finish() resets the object, so the next message hashes like a fresh one. */
TEST(Small_Sha256, FinishResetsTheHashForANewMessage)
{
    Sha256 hash;
    hash.update(bytes_of("first message"));
    static_cast<void>(hash.finish());

    hash.update(bytes_of("abc"));

    EXPECT_EQ(to_hex(hash.finish()), K_ABC_DIGEST);
}
