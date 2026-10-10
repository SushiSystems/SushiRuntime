/**************************************************************************/
/* hmac_sha256.hpp                                                        */
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
 * @file hmac_sha256.hpp
 * @brief Declares HmacSha256 (RFC 2104) and the helpers that compare and wipe secret bytes.
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>

#include "sha256.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Authenticates a message fed in pieces with HMAC-SHA256 (RFC 2104).
         *
         * The key block lives inside the object, never on the heap, and the destructor wipes it.
         */
        class SUSHIRUNTIME_API HmacSha256 final
        {
            public:
                /** @brief Starts a message under a key of any length, including none. */
                explicit HmacSha256(span<const std::uint8_t> key) noexcept;

                /** @brief Wipes the key block. */
                ~HmacSha256();

                HmacSha256(const HmacSha256&) = delete;
                HmacSha256& operator=(const HmacSha256&) = delete;
                HmacSha256(HmacSha256&&) = delete;
                HmacSha256& operator=(HmacSha256&&) = delete;

                /** @brief Appends bytes to the message; an empty span changes nothing. */
                void update(span<const std::uint8_t> bytes) noexcept;

                /**
                 * @brief Returns the authentication code of the bytes appended so far.
                 * @pre finish() has not been called on this object.
                 */
                [[nodiscard]] Sha256Digest finish() noexcept;

            private:
                /** @brief Hashes the key block XOR ipad, then the message. */
                Sha256 inner_;

                /** @brief Holds the key block XOR opad until finish(). */
                std::array<std::uint8_t, K_SHA256_BLOCK_BYTES> outer_pad_;
        };

        /** @brief Returns the HMAC-SHA256 code of one whole message. */
        [[nodiscard]] SUSHIRUNTIME_API Sha256Digest hmac_sha256(
            span<const std::uint8_t> key, span<const std::uint8_t> message) noexcept;

        /**
         * @brief Reports whether two digests are equal, in a time that does not depend on
         *        where they differ.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool equal_in_constant_time(
            const Sha256Digest& left, const Sha256Digest& right) noexcept;

        /**
         * @brief Overwrites bytes with zeros through a volatile pointer, so the stores survive
         *        optimisation.
         * @param bytes May be null when @p size is zero.
         */
        SUSHIRUNTIME_API void wipe_bytes(std::uint8_t* bytes, std::size_t size) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
