/**************************************************************************/
/* sha256.hpp                                                             */
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
 * @file sha256.hpp
 * @brief Declares Sha256, the FIPS 180-4 SHA-256 hash, and its digest type.
 * @author Mustafa Garip
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Counts the bytes of a SHA-256 digest. */
        inline constexpr std::size_t K_SHA256_DIGEST_BYTES = 32;

        /** @brief Counts the bytes of one SHA-256 input block. */
        inline constexpr std::size_t K_SHA256_BLOCK_BYTES = 64;

        /** @brief Holds a SHA-256 digest, most significant byte of each word first. */
        using Sha256Digest = std::array<std::uint8_t, K_SHA256_DIGEST_BYTES>;

        /**
         * @brief Hashes a message fed in pieces with SHA-256 (FIPS 180-4).
         *
         * The object holds its whole state inline and never allocates.
         */
        class SUSHIRUNTIME_API Sha256 final
        {
            public:
                /** @brief Starts an empty message. */
                Sha256() noexcept;

                /** @brief Appends bytes to the message; an empty span changes nothing. */
                void update(span<const std::uint8_t> bytes) noexcept;

                /**
                 * @brief Returns the digest of the bytes appended so far.
                 *
                 * The object then holds an empty message again and can hash another one.
                 */
                [[nodiscard]] Sha256Digest finish() noexcept;

            private:
                /** @brief Clears the buffered bytes and restores the initial hash value. */
                void reset() noexcept;

                /** @brief Holds the eight working words H0 to H7. */
                std::array<std::uint32_t, 8> state_;

                /** @brief Holds the bytes of the block that is not full yet. */
                std::array<std::uint8_t, K_SHA256_BLOCK_BYTES> block_;

                /** @brief Counts the bytes held in block_. */
                std::size_t block_fill_;

                /** @brief Counts every byte appended to the message. */
                std::uint64_t total_bytes_;
        };

        /** @brief Returns the SHA-256 digest of one whole message. */
        [[nodiscard]] SUSHIRUNTIME_API Sha256Digest sha256(span<const std::uint8_t> bytes) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
