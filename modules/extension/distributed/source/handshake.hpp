/**************************************************************************/
/* handshake.hpp                                                          */
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
 * @file handshake.hpp
 * @brief Declares the nonce and the two proofs of the protocol v2 handshake.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>

#include "sha256.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Counts the bytes of a handshake nonce. */
        inline constexpr std::size_t K_HANDSHAKE_NONCE_BYTES = 32;

        /** @brief Holds the random bytes one side contributes to a handshake. */
        struct HandshakeNonce
        {
            /** @brief Holds the nonce as it travels in HELLO or CHALLENGE. */
            std::array<std::uint8_t, K_HANDSHAKE_NONCE_BYTES> bytes;
        };

        /** @brief Names the side that proves it holds the shared secret. */
        enum class HandshakeRole : std::uint8_t
        {
            Master, /**< The master, whose proof travels in CHALLENGE. */
            Worker, /**< The worker, whose proof travels in PROOF. */
        };

        /** @brief Produces one nonce per call; a session takes one so a test can fix it. */
        using NonceSource = std::function<HandshakeNonce()>;

        /**
         * @brief Returns a nonce drawn from std::random_device.
         * @throws SushiException With ErrorCode::Unsupported when the platform has no
         *         random source.
         */
        [[nodiscard]] SUSHIRUNTIME_API HandshakeNonce make_nonce();

        /**
         * @brief Returns HMAC(secret, label of @p prover || worker_nonce || master_nonce).
         * @param secret The shared secret; empty runs the same exchange under an empty key.
         */
        [[nodiscard]] SUSHIRUNTIME_API Sha256Digest compute_proof(
            HandshakeRole prover, span<const std::uint8_t> secret,
            const HandshakeNonce& worker_nonce, const HandshakeNonce& master_nonce) noexcept;

        /**
         * @brief Reports whether @p received is the proof @p prover owes for these nonces,
         *        compared in constant time.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool verify_proof(
            HandshakeRole prover, span<const std::uint8_t> secret,
            const HandshakeNonce& worker_nonce, const HandshakeNonce& master_nonce,
            const Sha256Digest& received) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
