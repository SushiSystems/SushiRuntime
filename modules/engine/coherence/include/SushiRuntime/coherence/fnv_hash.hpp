/**************************************************************************/
/* fnv_hash.hpp                                                           */
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
 * @file fnv_hash.hpp
 * @brief Declares FNVHash, the 64-bit FNV-1a accumulator every coherence hash folds into.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Accumulates a 64-bit FNV-1a hash over values folded as eight little-endian bytes.
         *
         * The result depends only on the folded values and their order, never on the machine.
         */
        class FNVHash final
        {
            public:
                /** @brief Folds the eight little-endian bytes of value into the hash. */
                constexpr void fold(std::uint64_t value) noexcept
                {
                    for (unsigned shift = 0; shift < 64; shift += 8)
                    {
                        hash_ ^= (value >> shift) & 0xffU;
                        hash_ *= K_PRIME;
                    }
                }

                /** @brief Returns the hash of everything folded so far. */
                [[nodiscard]] constexpr std::uint64_t value() const noexcept
                {
                    return hash_;
                }

            private:
                /// @brief The FNV-1a 64-bit offset basis.
                static constexpr std::uint64_t K_OFFSET = 0xcbf29ce484222325ULL;

                /// @brief The FNV-1a 64-bit prime.
                static constexpr std::uint64_t K_PRIME = 0x100000001b3ULL;

                /// @brief The hash so far.
                std::uint64_t hash_ = K_OFFSET;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
