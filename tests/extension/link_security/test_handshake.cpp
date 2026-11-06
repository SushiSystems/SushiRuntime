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
 * @file test_handshake.cpp
 * @brief Tests the handshake proofs: the values, the roles, the secret, the nonces and the platform nonce source.
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

    /** @brief Holds the worker nonce of the handshake every test in this file records. */
    constexpr std::uint8_t K_WORKER_NONCE = 0x11;

    /** @brief Holds the master nonce of the handshake every test in this file records. */
    constexpr std::uint8_t K_MASTER_NONCE = 0x22;

    /** @brief Returns the name of a role for a test report. */
    std::string name_of_role(HandshakeRole role)
    {
        return role == HandshakeRole::Master ? "Master" : "Worker";
    }

    /** @brief Holds the proof of one role for the recorded handshake. */
    struct ProofRow
    {
        HandshakeRole role; /**< The role that computes the proof. */
        const char* proof;  /**< The expected proof, 64 lowercase hexadecimal digits. */
    };

    /** @brief Names a proof row in a test report. */
    std::string name_of_proof(const ::testing::TestParamInfo<ProofRow>& info)
    {
        return name_of_role(info.param.role);
    }

    /** @brief Names a role row in a test report. */
    std::string name_of_role_row(const ::testing::TestParamInfo<HandshakeRole>& info)
    {
        return name_of_role(info.param);
    }

    /**
     * @brief Holds the proofs for the secret of bytes 0 to 31 and the recorded nonces.
     *
     * Computed with the hmac module of Python over the labels `SRDP2 master` and `SRDP2 worker`,
     * the worker nonce 32 x 0x11 and the master nonce 32 x 0x22. A change of the label, of the
     * order of the nonces or of the key is a change to the wire protocol.
     */
    const ProofRow K_RECORDED_PROOFS[] = {
        {HandshakeRole::Master, "228855beb9cdc5298f9fe8a2da682d24d692884990f488ad24c1ec94988c031e"},
        {HandshakeRole::Worker, "2b7db168d24e1b61ab59423115a162615b8f1c47a0a137e659f8d8d55d8ac225"},
    };
} // namespace

/** @brief Runs a test once per proof of the recorded handshake. */
class Small_HandshakeProofTest : public ::testing::TestWithParam<ProofRow>
{
};

/** @brief Tests a proof against the value from an independent implementation for the same input. */
TEST_P(Small_HandshakeProofTest, ProofMatchesAnIndependentImplementation)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);

    EXPECT_EQ(to_hex(compute_proof(GetParam().role, bytes_of(secret), nonce_of(K_WORKER_NONCE),
                                   nonce_of(K_MASTER_NONCE))),
              GetParam().proof);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_HandshakeProofTest, ::testing::ValuesIn(K_RECORDED_PROOFS),
                         name_of_proof);

/** @brief Tests that one side's proof cannot be sent back as the other's. */
TEST(Small_HandshakeTest, ProofsDifferByRole)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const HandshakeNonce worker_nonce = nonce_of(K_WORKER_NONCE);
    const HandshakeNonce master_nonce = nonce_of(K_MASTER_NONCE);

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

/** @brief Runs a test once per role. */
class Small_HandshakeRoleTest : public ::testing::TestWithParam<HandshakeRole>
{
};

/** @brief Tests that a proof made under another secret is refused. */
TEST_P(Small_HandshakeRoleTest, AProofUnderAnotherSecretIsRefused)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    std::vector<std::uint8_t> other = secret;
    other.back() ^= 0x01u;
    const HandshakeNonce worker_nonce = nonce_of(K_WORKER_NONCE);
    const HandshakeNonce master_nonce = nonce_of(K_MASTER_NONCE);
    const Sha256Digest forged =
        compute_proof(GetParam(), bytes_of(other), worker_nonce, master_nonce);

    EXPECT_FALSE(verify_proof(GetParam(), bytes_of(secret), worker_nonce, master_nonce, forged));
}

/** @brief Tests that a proof made under no secret is refused where a secret is set. */
TEST_P(Small_HandshakeRoleTest, AProofUnderNoSecretIsRefused)
{
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const HandshakeNonce worker_nonce = nonce_of(K_WORKER_NONCE);
    const HandshakeNonce master_nonce = nonce_of(K_MASTER_NONCE);
    const Sha256Digest keyless = compute_proof(GetParam(), {}, worker_nonce, master_nonce);

    EXPECT_FALSE(verify_proof(GetParam(), bytes_of(secret), worker_nonce, master_nonce, keyless));
}

/** @brief Tests that a proof made under no secret is accepted where no secret is set. */
TEST_P(Small_HandshakeRoleTest, AProofUnderNoSecretIsAcceptedWhereNoneIsSet)
{
    const HandshakeNonce worker_nonce = nonce_of(K_WORKER_NONCE);
    const HandshakeNonce master_nonce = nonce_of(K_MASTER_NONCE);
    const Sha256Digest keyless = compute_proof(GetParam(), {}, worker_nonce, master_nonce);

    EXPECT_TRUE(verify_proof(GetParam(), {}, worker_nonce, master_nonce, keyless));
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_HandshakeRoleTest,
                         ::testing::Values(HandshakeRole::Master, HandshakeRole::Worker),
                         name_of_role_row);

namespace
{
    /** @brief Holds the nonces a replayed proof is verified under. */
    struct ReplayRow
    {
        const char* meaning;       /**< Names the replay in a test report. */
        HandshakeRole role;        /**< The role whose recorded proof is replayed. */
        std::uint8_t worker_nonce; /**< The worker nonce the verifier contributes. */
        std::uint8_t master_nonce; /**< The master nonce the verifier contributes. */
    };

    /** @brief Names a replay row in a test report. */
    std::string name_of_replay(const ::testing::TestParamInfo<ReplayRow>& info)
    {
        return info.param.meaning;
    }

    const ReplayRow K_REPLAYS[] = {
        {"FreshMasterNonce", HandshakeRole::Worker, K_WORKER_NONCE, 0x44},
        {"FreshWorkerNonce", HandshakeRole::Master, 0x33, K_MASTER_NONCE},
        {"NoncesSwapped", HandshakeRole::Worker, K_MASTER_NONCE, K_WORKER_NONCE},
    };
} // namespace

/** @brief Runs a test once per way a verifier's nonces can differ from the recorded ones. */
class Small_HandshakeReplayTest : public ::testing::TestWithParam<ReplayRow>
{
};

/** @brief Tests that a proof recorded in one handshake fails once a nonce changes. */
TEST_P(Small_HandshakeReplayTest, FreshNonceDefeatsReplay)
{
    const ReplayRow& row = GetParam();
    const std::vector<std::uint8_t> secret = counting(0x00, 32);
    const Sha256Digest recorded = compute_proof(
        row.role, bytes_of(secret), nonce_of(K_WORKER_NONCE), nonce_of(K_MASTER_NONCE));

    EXPECT_FALSE(verify_proof(row.role, bytes_of(secret), nonce_of(row.worker_nonce),
                              nonce_of(row.master_nonce), recorded));
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_HandshakeReplayTest, ::testing::ValuesIn(K_REPLAYS),
                         name_of_replay);

/** @brief Tests that the platform's nonce source does not hand out one nonce twice. */
TEST(Medium_HandshakeTest, PlatformNonceSourceDoesNotRepeat)
{
    const HandshakeNonce first = make_nonce().value();
    const HandshakeNonce second = make_nonce().value();

    EXPECT_NE(first.bytes, second.bytes);
}

/** @brief Tests that the platform's nonce source does not hand out an all-zero nonce. */
TEST(Medium_HandshakeTest, PlatformNonceSourceIsNotAllZero)
{
    const HandshakeNonce nonce = make_nonce().value();

    EXPECT_NE(nonce.bytes, nonce_of(0x00).bytes);
}
