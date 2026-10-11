/**************************************************************************/
/* test_handshake.cpp                                                     */
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
 * @file test_handshake.cpp
 * @brief Tests SHA-256, HMAC-SHA256, the handshake proofs and SharedSecret.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>

#include "handshake.hpp"
#include "hmac_sha256.hpp"
#include "sha256.hpp"

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

    /** @brief Returns a nonce whose bytes all hold @p value. */
    HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce;
        nonce.bytes.fill(value);
        return nonce;
    }

    /** @brief Returns the error of a refused secret, or nothing when one was built. */
    std::optional<Error> error_of(const Result<SharedSecret>& secret)
    {
        if (secret.has_value())
            return std::nullopt;
        return secret.error();
    }

    /** @brief Writes a file in the working directory and removes it when destroyed. */
    class ScratchFile final
    {
        public:
            /** @brief Writes @p bytes to a file called @p name in the working directory. */
            ScratchFile(const std::string& name, const std::vector<std::uint8_t>& bytes)
                : path_(std::filesystem::current_path() / name)
            {
                std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }

            /** @brief Removes the file. */
            ~ScratchFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }

            ScratchFile(const ScratchFile&) = delete;
            ScratchFile& operator=(const ScratchFile&) = delete;
            ScratchFile(ScratchFile&&) = delete;
            ScratchFile& operator=(ScratchFile&&) = delete;

            /** @brief Returns where the file is. */
            const std::filesystem::path& path() const noexcept
            {
                return path_;
            }

        private:
            /** @brief Holds the absolute path of the file. */
            std::filesystem::path path_;
    };
}

/** @brief Tests SHA-256 against the example messages of FIPS 180-4. */
TEST(Unit_Sha256Test, Sha256Fips180Vectors)
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
TEST(Unit_Sha256Test, PiecewiseUpdateMatchesOneShot)
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

/** @brief Tests HMAC-SHA256 against test cases 1, 2, 3, 4, 6 and 7 of RFC 4231. */
TEST(Unit_HmacSha256Test, HmacRfc4231Vectors)
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
TEST(Unit_HmacSha256Test, EqualInConstantTimeSeesEveryByte)
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

/** @brief Tests the two proofs against values from Python's hmac module for the same input. */
TEST(Unit_HandshakeTest, ProofsMatchAnIndependentImplementation)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);

    EXPECT_EQ(to_hex(compute_proof(HandshakeRole::Master, bytes_of(secret), worker_nonce,
                                   master_nonce)),
              "228855beb9cdc5298f9fe8a2da682d24d692884990f488ad24c1ec94988c031e");
    EXPECT_EQ(to_hex(compute_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce,
                                   master_nonce)),
              "2b7db168d24e1b61ab59423115a162615b8f1c47a0a137e659f8d8d55d8ac225");
}

/** @brief Tests that one side's proof cannot be sent back as the other's. */
TEST(Unit_HandshakeTest, ProofsDifferByRole)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);

    const Sha256Digest master_proof =
        compute_proof(HandshakeRole::Master, bytes_of(secret), worker_nonce, master_nonce);
    const Sha256Digest worker_proof =
        compute_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce, master_nonce);

    EXPECT_NE(master_proof, worker_proof);
    EXPECT_TRUE(verify_proof(HandshakeRole::Master, bytes_of(secret), worker_nonce,
                             master_nonce, master_proof));
    EXPECT_TRUE(verify_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce,
                             master_nonce, worker_proof));
    EXPECT_FALSE(verify_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce,
                              master_nonce, master_proof));
    EXPECT_FALSE(verify_proof(HandshakeRole::Master, bytes_of(secret), worker_nonce,
                              master_nonce, worker_proof));
}

/** @brief Tests that a proof made under another secret, or under none, is refused. */
TEST(Unit_HandshakeTest, WrongSecretFails)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    std::vector<std::uint8_t> other = secret;
    other.back() ^= 0x01u;
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);

    for (const HandshakeRole role : { HandshakeRole::Master, HandshakeRole::Worker })
    {
        const Sha256Digest forged =
            compute_proof(role, bytes_of(other), worker_nonce, master_nonce);
        const Sha256Digest keyless = compute_proof(role, {}, worker_nonce, master_nonce);

        EXPECT_FALSE(verify_proof(role, bytes_of(secret), worker_nonce, master_nonce, forged));
        EXPECT_FALSE(verify_proof(role, bytes_of(secret), worker_nonce, master_nonce, keyless));
        EXPECT_TRUE(verify_proof(role, {}, worker_nonce, master_nonce, keyless));
    }
}

/** @brief Tests that a proof recorded in one handshake fails once either nonce changes. */
TEST(Unit_HandshakeTest, FreshNonceDefeatsReplay)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const HandshakeNonce worker_nonce = nonce_of(0x11);
    const HandshakeNonce master_nonce = nonce_of(0x22);
    const HandshakeNonce fresh_worker_nonce = nonce_of(0x33);
    const HandshakeNonce fresh_master_nonce = nonce_of(0x44);

    const Sha256Digest recorded_worker_proof =
        compute_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce, master_nonce);
    const Sha256Digest recorded_master_proof =
        compute_proof(HandshakeRole::Master, bytes_of(secret), worker_nonce, master_nonce);

    EXPECT_FALSE(verify_proof(HandshakeRole::Worker, bytes_of(secret), worker_nonce,
                              fresh_master_nonce, recorded_worker_proof));
    EXPECT_FALSE(verify_proof(HandshakeRole::Master, bytes_of(secret), fresh_worker_nonce,
                              master_nonce, recorded_master_proof));
    EXPECT_FALSE(verify_proof(HandshakeRole::Worker, bytes_of(secret), master_nonce,
                              worker_nonce, recorded_worker_proof));
}

/** @brief Tests that the platform's nonce source does not hand out one nonce twice. */
TEST(Unit_HandshakeTest, PlatformNonceSourceDoesNotRepeat)
{
    const HandshakeNonce first = make_nonce().value();
    const HandshakeNonce second = make_nonce().value();

    EXPECT_NE(first.bytes, second.bytes);
    EXPECT_NE(first.bytes, nonce_of(0x00).bytes);
}

/** @brief Tests that a file one byte under the minimum is refused, by name and length. */
TEST(Unit_SharedSecretTest, FromFileRefusesAShortFile)
{
    const ScratchFile file("sr_test_shared_secret_short.bin", counting(0x00, 31));

    const std::optional<Error> error =
        error_of(SharedSecret::from_file(file.path()));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find("sr_test_shared_secret_short.bin"), std::string::npos);
    EXPECT_NE(error->message.find("31 bytes"), std::string::npos);
}

/** @brief Tests that a file of exactly the minimum length is read byte for byte. */
TEST(Unit_SharedSecretTest, FromFileAcceptsThirtyTwoBytes)
{
    const std::vector<std::uint8_t> content = counting(0xe0, 32);
    const ScratchFile file("sr_test_shared_secret_exact.bin", content);

    const SharedSecret secret = SharedSecret::from_file(file.path()).value();

    EXPECT_FALSE(secret.empty());
    ASSERT_EQ(secret.size(), content.size());
    EXPECT_EQ(std::vector<std::uint8_t>(secret.bytes().begin(), secret.bytes().end()), content);
}

/** @brief Tests that a missing file is refused with its path in the message. */
TEST(Unit_SharedSecretTest, FromFileMissingNamesThePath)
{
    const std::filesystem::path missing =
        std::filesystem::current_path() / "sr_test_shared_secret_missing.bin";

    const std::optional<Error> error =
        error_of(SharedSecret::from_file(missing));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find(missing.generic_string()), std::string::npos);
}

/** @brief Tests that an unset environment variable yields the empty secret. */
TEST(Unit_SharedSecretTest, FromEnvironmentUnsetIsEmpty)
{
    const SharedSecret secret =
        SharedSecret::from_environment("SR_TEST_SHARED_SECRET_VARIABLE_NOBODY_SETS").value();

    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.size(), 0u);
}

/** @brief Tests that a move empties its source and a clone leaves it whole. */
TEST(Unit_SharedSecretTest, MoveEmptiesTheSourceAndCloneKeepsIt)
{
    const std::vector<std::uint8_t> content = counting(0x40, 48);
    SharedSecret original = SharedSecret::from_bytes(bytes_of(content)).value();

    const SharedSecret copy = original.clone();
    EXPECT_EQ(original.size(), content.size());
    EXPECT_EQ(std::vector<std::uint8_t>(copy.bytes().begin(), copy.bytes().end()), content);

    const SharedSecret moved = std::move(original);
    EXPECT_EQ(moved.size(), content.size());
    EXPECT_TRUE(original.empty());
    EXPECT_TRUE(SharedSecret().empty());
}

/** @brief Tests that bytes passed in memory meet the same minimum as a file. */
TEST(Unit_SharedSecretTest, FromBytesRefusesAShortSecret)
{
    const std::vector<std::uint8_t> content = counting(0x00, 31);

    const std::optional<Error> error =
        error_of(SharedSecret::from_bytes(bytes_of(content)));

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, ErrorCode::InvalidAccess);
    EXPECT_NE(error->message.find("31 bytes"), std::string::npos);
}
