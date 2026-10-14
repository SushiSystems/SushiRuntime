/**************************************************************************/
/* sha256.cpp                                                             */
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
 * @file sha256.cpp
 * @brief Implements Sha256, the FIPS 180-4 SHA-256 hash.
 * @author Mustafa Garip
 */

#include <SushiRuntime/link_security/sha256.hpp>

#include <algorithm>
#include <cstring>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Counts the message schedule words of one block. */
            constexpr std::size_t K_SCHEDULE_WORDS = 64;

            /** @brief Marks where the 64-bit message length starts in the last block. */
            constexpr std::size_t K_LENGTH_OFFSET = K_SHA256_BLOCK_BYTES - 8;

            /** @brief Holds the initial hash value of FIPS 180-4, section 5.3.3. */
            constexpr std::array<std::uint32_t, 8> K_INITIAL_STATE = {
                0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

            /** @brief Holds the round constants of FIPS 180-4, section 4.2.2. */
            constexpr std::array<std::uint32_t, K_SCHEDULE_WORDS> K_ROUND_CONSTANTS = {
                0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
                0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
                0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
                0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
                0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
                0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
                0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
                0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
                0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

            /** @brief Rotates a word right by a count between 1 and 31. */
            constexpr std::uint32_t rotate_right(std::uint32_t word, unsigned count) noexcept
            {
                return (word >> count) | (word << (32u - count));
            }

            /** @brief Reads a big-endian 32-bit word from four bytes. */
            constexpr std::uint32_t load_big_endian(const std::uint8_t* bytes) noexcept
            {
                return (static_cast<std::uint32_t>(bytes[0]) << 24)
                    | (static_cast<std::uint32_t>(bytes[1]) << 16)
                    | (static_cast<std::uint32_t>(bytes[2]) << 8)
                    | static_cast<std::uint32_t>(bytes[3]);
            }

            /** @brief Writes a 32-bit word to four bytes, most significant first. */
            void store_big_endian(std::uint32_t word, std::uint8_t* bytes) noexcept
            {
                bytes[0] = static_cast<std::uint8_t>(word >> 24);
                bytes[1] = static_cast<std::uint8_t>(word >> 16);
                bytes[2] = static_cast<std::uint8_t>(word >> 8);
                bytes[3] = static_cast<std::uint8_t>(word);
            }

            /**
             * @brief Folds one block into the hash value (FIPS 180-4, section 6.2.2).
             * @param block Exactly K_SHA256_BLOCK_BYTES readable bytes.
             */
            void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) noexcept
            {
                std::array<std::uint32_t, K_SCHEDULE_WORDS> schedule;
                for (std::size_t index = 0; index < 16; ++index)
                    schedule[index] = load_big_endian(block + index * 4);

                for (std::size_t index = 16; index < K_SCHEDULE_WORDS; ++index)
                {
                    const std::uint32_t early = schedule[index - 15];
                    const std::uint32_t late = schedule[index - 2];
                    const std::uint32_t small_sigma_0 =
                        rotate_right(early, 7) ^ rotate_right(early, 18) ^ (early >> 3);
                    const std::uint32_t small_sigma_1 =
                        rotate_right(late, 17) ^ rotate_right(late, 19) ^ (late >> 10);
                    schedule[index] =
                        schedule[index - 16] + small_sigma_0 + schedule[index - 7] + small_sigma_1;
                }

                std::uint32_t a = state[0];
                std::uint32_t b = state[1];
                std::uint32_t c = state[2];
                std::uint32_t d = state[3];
                std::uint32_t e = state[4];
                std::uint32_t f = state[5];
                std::uint32_t g = state[6];
                std::uint32_t h = state[7];

                for (std::size_t index = 0; index < K_SCHEDULE_WORDS; ++index)
                {
                    const std::uint32_t big_sigma_1 =
                        rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
                    const std::uint32_t choice = (e & f) ^ (~e & g);
                    const std::uint32_t first =
                        h + big_sigma_1 + choice + K_ROUND_CONSTANTS[index] + schedule[index];
                    const std::uint32_t big_sigma_0 =
                        rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
                    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t second = big_sigma_0 + majority;

                    h = g;
                    g = f;
                    f = e;
                    e = d + first;
                    d = c;
                    c = b;
                    b = a;
                    a = first + second;
                }

                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
                state[4] += e;
                state[5] += f;
                state[6] += g;
                state[7] += h;
            }
        } // namespace

        Sha256::Sha256() noexcept
        {
            reset();
        }

        void Sha256::update(span<const std::uint8_t> bytes) noexcept
        {
            const std::uint8_t* cursor = bytes.data();
            std::size_t remaining = bytes.size();
            if (remaining == 0)
                return;

            total_bytes_ += remaining;

            if (block_fill_ != 0)
            {
                const std::size_t taken = std::min(remaining, K_SHA256_BLOCK_BYTES - block_fill_);
                std::memcpy(block_.data() + block_fill_, cursor, taken);
                block_fill_ += taken;
                cursor += taken;
                remaining -= taken;
                if (block_fill_ < K_SHA256_BLOCK_BYTES)
                    return;

                compress(state_, block_.data());
                block_fill_ = 0;
            }

            while (remaining >= K_SHA256_BLOCK_BYTES)
            {
                compress(state_, cursor);
                cursor += K_SHA256_BLOCK_BYTES;
                remaining -= K_SHA256_BLOCK_BYTES;
            }

            if (remaining != 0)
            {
                std::memcpy(block_.data(), cursor, remaining);
                block_fill_ = remaining;
            }
        }

        Sha256Digest Sha256::finish() noexcept
        {
            const std::uint64_t total_bits = total_bytes_ * 8u;

            // block_fill_ is below the block size here, so the padding byte always fits.
            block_[block_fill_++] = 0x80u;
            if (block_fill_ > K_LENGTH_OFFSET)
            {
                std::fill(block_.begin() + block_fill_, block_.end(), std::uint8_t{0});
                compress(state_, block_.data());
                block_fill_ = 0;
            }

            std::fill(block_.begin() + block_fill_, block_.begin() + K_LENGTH_OFFSET,
                      std::uint8_t{0});
            store_big_endian(static_cast<std::uint32_t>(total_bits >> 32),
                             block_.data() + K_LENGTH_OFFSET);
            store_big_endian(static_cast<std::uint32_t>(total_bits),
                             block_.data() + K_LENGTH_OFFSET + 4);
            compress(state_, block_.data());

            Sha256Digest digest;
            for (std::size_t index = 0; index < state_.size(); ++index)
                store_big_endian(state_[index], digest.data() + index * 4);

            reset();
            return digest;
        }

        void Sha256::reset() noexcept
        {
            state_ = K_INITIAL_STATE;
            block_.fill(0);
            block_fill_ = 0;
            total_bytes_ = 0;
        }

        Sha256Digest sha256(span<const std::uint8_t> bytes) noexcept
        {
            Sha256 hash;
            hash.update(bytes);
            return hash.finish();
        }

    } // namespace Distributed
} // namespace SushiRuntime
