/**************************************************************************/
/* handshake.cpp                                                          */
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
 * @file handshake.cpp
 * @brief Implements the nonce and the two proofs of the protocol v2 handshake.
 * @author Mustafa Garip
 */

#include "handshake.hpp"

#include <exception>
#include <random>
#include <string>
#include <string_view>

#include <SushiRuntime/core/error.hpp>

#include "hmac_sha256.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the label each role's proof starts with, indexed by HandshakeRole. */
            constexpr std::array<std::string_view, 2> K_ROLE_LABELS = {
                "SRDP2 master", "SRDP2 worker"};

            /** @brief Counts the bytes one std::random_device draw contributes to a nonce. */
            constexpr std::size_t K_BYTES_PER_DRAW = 4;

            /** @brief Returns the bytes of a nonce as a span. */
            span<const std::uint8_t> bytes_of(const HandshakeNonce& nonce) noexcept
            {
                return span<const std::uint8_t>(nonce.bytes.data(), nonce.bytes.size());
            }
        } // namespace

        HandshakeNonce make_nonce()
        {
            HandshakeNonce nonce{};
            std::string failure;
            try
            {
                std::random_device device;
                for (std::size_t offset = 0; offset < nonce.bytes.size();
                     offset += K_BYTES_PER_DRAW)
                {
                    // Only the low 32 bits of a draw are used; the standard promises no more.
                    const std::uint32_t draw = static_cast<std::uint32_t>(device());
                    for (std::size_t index = 0; index < K_BYTES_PER_DRAW; ++index)
                        nonce.bytes[offset + index] =
                            static_cast<std::uint8_t>(draw >> (8 * index));
                }

                return nonce;
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }

            throw Core::SushiException(Core::Error{
                Core::Errc::unsupported,
                "cannot draw a handshake nonce from std::random_device: " + failure});
        }

        Sha256Digest compute_proof(HandshakeRole prover, span<const std::uint8_t> secret,
                                   const HandshakeNonce& worker_nonce,
                                   const HandshakeNonce& master_nonce) noexcept
        {
            const std::string_view label = K_ROLE_LABELS[static_cast<std::size_t>(prover)];

            HmacSha256 authenticator(secret);
            authenticator.update(span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(label.data()), label.size()));
            authenticator.update(bytes_of(worker_nonce));
            authenticator.update(bytes_of(master_nonce));
            return authenticator.finish();
        }

        bool verify_proof(HandshakeRole prover, span<const std::uint8_t> secret,
                          const HandshakeNonce& worker_nonce, const HandshakeNonce& master_nonce,
                          const Sha256Digest& received) noexcept
        {
            const Sha256Digest expected =
                compute_proof(prover, secret, worker_nonce, master_nonce);
            return equal_in_constant_time(expected, received);
        }

    } // namespace Distributed
} // namespace SushiRuntime
