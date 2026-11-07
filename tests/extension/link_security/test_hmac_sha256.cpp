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
 * @file test_hmac_sha256.cpp
 * @brief Tests hmac_sha256: the RFC 4231 vectors and the constant-time comparison.
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

#include "support/digest_bytes.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using TestSupport::bytes_of;
using TestSupport::to_hex;

namespace
{
    /** @brief Names the bytes of a key or a message. */
    using Bytes = std::vector<std::uint8_t>;

    /** @brief Returns the bytes of a text. */
    Bytes bytes_from(std::string_view text)
    {
        return Bytes(text.begin(), text.end());
    }

    /** @brief Returns @p count bytes that all hold @p value. */
    Bytes repeated(std::uint8_t value, std::size_t count)
    {
        return Bytes(count, value);
    }

    /** @brief Returns the bytes first, first + 1, ... of the given count. */
    Bytes counting(std::uint8_t first, std::size_t count)
    {
        Bytes bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::uint8_t>(first + index);
        return bytes;
    }

    /** @brief Holds one test case of RFC 4231 and the code the RFC gives it. */
    struct MacRow
    {
        const char* meaning; /**< Names the case in a test report. */
        Bytes key;           /**< The key of the case. */
        Bytes data;          /**< The message of the case. */
        const char* mac;     /**< The published code, 64 lowercase hexadecimal digits. */
    };

    /** @brief Names a row by its meaning in a test report. */
    std::string name_of(const ::testing::TestParamInfo<MacRow>& info)
    {
        return info.param.meaning;
    }

    /**
     * @brief Holds test cases 1, 2, 3, 4, 6 and 7 of RFC 4231.
     *
     * Case 5 truncates the code to 128 bits, which the handshake never does.
     */
    const MacRow K_RFC_4231_CASES[] = {
        {"Case1", repeated(0x0b, 20), bytes_from("Hi There"),
         "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
        {"Case2", bytes_from("Jefe"), bytes_from("what do ya want for nothing?"),
         "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
        {"Case3", repeated(0xaa, 20), repeated(0xdd, 50),
         "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"},
        {"Case4", counting(0x01, 25), repeated(0xcd, 50),
         "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"},
        {"Case6KeyLongerThanABlock", repeated(0xaa, 131),
         bytes_from("Test Using Larger Than Block-Size Key - Hash Key First"),
         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
        {"Case7KeyAndDataLongerThanABlock", repeated(0xaa, 131),
         bytes_from("This is a test using a larger than block-size key and a larger "
                    "than block-size data. The key needs to be hashed before being "
                    "used by the HMAC algorithm."),
         "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"},
    };
} // namespace

/** @brief Runs a test once per RFC 4231 case. */
class Small_HmacSha256Test : public ::testing::TestWithParam<MacRow>
{
};

/** @brief Tests HMAC-SHA256 against the RFC 4231 cases; 6 and 7 hash the key first. */
TEST_P(Small_HmacSha256Test, HmacRfc4231Vectors)
{
    const MacRow& row = GetParam();

    EXPECT_EQ(to_hex(hmac_sha256(bytes_of(row.key), bytes_of(row.data))), row.mac);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_HmacSha256Test, ::testing::ValuesIn(K_RFC_4231_CASES),
                         name_of);

/** @brief Tests that the constant-time comparison accepts two equal digests. */
TEST(Small_HmacSha256, EqualInConstantTimeAcceptsEqualDigests)
{
    const Sha256Digest first = sha256(bytes_of("abc"));
    const Sha256Digest second = sha256(bytes_of("abc"));

    EXPECT_TRUE(equal_in_constant_time(first, second));
}

/** @brief Runs a test once per byte position of a digest. */
class Small_HmacSha256Position : public ::testing::TestWithParam<std::size_t>
{
};

/** @brief Tests that the constant-time comparison sees a one-bit change in the given byte. */
TEST_P(Small_HmacSha256Position, EqualInConstantTimeSeesTheByte)
{
    const Sha256Digest digest = sha256(bytes_of("abc"));
    Sha256Digest altered = digest;
    altered[GetParam()] ^= 0x01u;

    EXPECT_FALSE(equal_in_constant_time(digest, altered));
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_HmacSha256Position,
                         ::testing::Range<std::size_t>(0, sizeof(Sha256Digest)));
