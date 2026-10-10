/**************************************************************************/
/* hmac_sha256.cpp                                                        */
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
 * @file hmac_sha256.cpp
 * @brief Implements HmacSha256 and the helpers that compare and wipe secret bytes.
 * @author Mustafa Garip
 */

#include "hmac_sha256.hpp"

#include <cstring>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the byte RFC 2104 XORs into the key block of the inner hash. */
            constexpr std::uint8_t K_INNER_PAD = 0x36u;

            /** @brief Holds the byte RFC 2104 XORs into the key block of the outer hash. */
            constexpr std::uint8_t K_OUTER_PAD = 0x5cu;
        } // namespace

        HmacSha256::HmacSha256(span<const std::uint8_t> key) noexcept
        {
            std::array<std::uint8_t, K_SHA256_BLOCK_BYTES> key_block{};
            if (key.size() > K_SHA256_BLOCK_BYTES)
            {
                const Sha256Digest hashed_key = sha256(key);
                std::memcpy(key_block.data(), hashed_key.data(), hashed_key.size());
            }
            else if (!key.empty())
            {
                std::memcpy(key_block.data(), key.data(), key.size());
            }

            std::array<std::uint8_t, K_SHA256_BLOCK_BYTES> inner_pad;
            for (std::size_t index = 0; index < K_SHA256_BLOCK_BYTES; ++index)
            {
                inner_pad[index] = static_cast<std::uint8_t>(key_block[index] ^ K_INNER_PAD);
                outer_pad_[index] = static_cast<std::uint8_t>(key_block[index] ^ K_OUTER_PAD);
            }

            inner_.update(span<const std::uint8_t>(inner_pad.data(), inner_pad.size()));
            wipe_bytes(key_block.data(), key_block.size());
            wipe_bytes(inner_pad.data(), inner_pad.size());
        }

        HmacSha256::~HmacSha256()
        {
            wipe_bytes(outer_pad_.data(), outer_pad_.size());
        }

        void HmacSha256::update(span<const std::uint8_t> bytes) noexcept
        {
            inner_.update(bytes);
        }

        Sha256Digest HmacSha256::finish() noexcept
        {
            const Sha256Digest inner_digest = inner_.finish();

            Sha256 outer;
            outer.update(span<const std::uint8_t>(outer_pad_.data(), outer_pad_.size()));
            outer.update(span<const std::uint8_t>(inner_digest.data(), inner_digest.size()));
            return outer.finish();
        }

        Sha256Digest hmac_sha256(span<const std::uint8_t> key,
                                 span<const std::uint8_t> message) noexcept
        {
            HmacSha256 authenticator(key);
            authenticator.update(message);
            return authenticator.finish();
        }

        bool equal_in_constant_time(const Sha256Digest& left, const Sha256Digest& right) noexcept
        {
            std::uint8_t difference = 0;
            for (std::size_t index = 0; index < left.size(); ++index)
                difference = static_cast<std::uint8_t>(difference | (left[index] ^ right[index]));

            return difference == 0;
        }

        void wipe_bytes(std::uint8_t* bytes, std::size_t size) noexcept
        {
            volatile std::uint8_t* cursor = bytes;
            for (std::size_t index = 0; index < size; ++index)
                cursor[index] = 0;
        }

    } // namespace Distributed
} // namespace SushiRuntime
