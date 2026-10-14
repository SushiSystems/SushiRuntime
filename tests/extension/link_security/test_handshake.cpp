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
 * @brief Tests the handshake proofs: known values, roles, wrong secrets, replay and nonces.
 * @see tests/extension/link_security/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>

#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/link_security/sha256.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
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
